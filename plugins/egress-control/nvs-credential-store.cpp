/******************************************************************************
    Copyright (C) 2026 by Aslam Iqbal

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "nvs-credential-store.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <util/util.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <dpapi.h>
#endif

namespace {

constexpr const char *CREDENTIAL_FILE_NAME = "nvs-credentials.bin";

/* Bound to the sealed blob so a file from another install cannot be swapped in
 * and silently decrypted. */
constexpr const char *ENTROPY = "NVS.DesktopCredentials.v1";

#ifdef _WIN32
QByteArray Seal(const QByteArray &plaintext, bool &ok)
{
	DATA_BLOB input = {};
	input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.constData()));
	input.cbData = static_cast<DWORD>(plaintext.size());

	QByteArray entropyBytes(ENTROPY);
	DATA_BLOB entropy = {};
	entropy.pbData = reinterpret_cast<BYTE *>(entropyBytes.data());
	entropy.cbData = static_cast<DWORD>(entropyBytes.size());

	DATA_BLOB output = {};

	if (!CryptProtectData(&input, L"NVS credentials", &entropy, nullptr, nullptr,
			      CRYPTPROTECT_UI_FORBIDDEN, &output)) {
		ok = false;
		return {};
	}

	QByteArray sealed(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
	LocalFree(output.pbData);

	ok = true;
	return sealed;
}

QByteArray Unseal(const QByteArray &sealed, bool &ok)
{
	DATA_BLOB input = {};
	input.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(sealed.constData()));
	input.cbData = static_cast<DWORD>(sealed.size());

	QByteArray entropyBytes(ENTROPY);
	DATA_BLOB entropy = {};
	entropy.pbData = reinterpret_cast<BYTE *>(entropyBytes.data());
	entropy.cbData = static_cast<DWORD>(entropyBytes.size());

	DATA_BLOB output = {};

	if (!CryptUnprotectData(&input, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
				&output)) {
		ok = false;
		return {};
	}

	QByteArray plaintext(reinterpret_cast<const char *>(output.pbData), static_cast<int>(output.cbData));
	SecureZeroMemory(output.pbData, output.cbData);
	LocalFree(output.pbData);

	ok = true;
	return plaintext;
}
#endif

} // namespace

bool NvsCredentialStore::IsEncryptionAvailable()
{
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

QString NvsCredentialStore::FilePath()
{
	BPtr<char> path = obs_module_get_config_path(obs_current_module(), CREDENTIAL_FILE_NAME);

	const char *raw = path;

	return raw ? QString::fromUtf8(raw) : QString();
}

bool NvsCredentialStore::Save(const QJsonObject &credentials)
{
	if (!IsEncryptionAvailable()) {
		blog(LOG_WARNING,
		     "[nvs] no credential encryption available on this platform; sign-in will not be remembered");
		return false;
	}

	const QString path = FilePath();

	if (path.isEmpty()) {
		return false;
	}

	QDir().mkpath(QFileInfo(path).absolutePath());

	QByteArray plaintext = QJsonDocument(credentials).toJson(QJsonDocument::Compact);

#ifdef _WIN32
	bool ok = false;
	const QByteArray sealed = Seal(plaintext, ok);

	/* Wipe the clear-text copy as soon as it is sealed. */
	plaintext.fill('\0');

	if (!ok) {
		blog(LOG_WARNING, "[nvs] failed to encrypt credentials; nothing was written");
		return false;
	}

	QFile file(path);

	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		blog(LOG_WARNING, "[nvs] could not open the credential store for writing");
		return false;
	}

	const bool written = file.write(sealed) == sealed.size();
	file.close();

	if (written) {
		/* Owner-only, matching the DPAPI scope. */
		QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	}

	return written;
#else
	plaintext.fill('\0');
	return false;
#endif
}

QJsonObject NvsCredentialStore::Load()
{
	if (!IsEncryptionAvailable()) {
		return {};
	}

	const QString path = FilePath();

	if (path.isEmpty() || !QFile::exists(path)) {
		return {};
	}

	QFile file(path);

	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}

	const QByteArray sealed = file.readAll();
	file.close();

#ifdef _WIN32
	bool ok = false;
	QByteArray plaintext = Unseal(sealed, ok);

	if (!ok) {
		/* Most often the file was copied from another machine or user. It is
		 * useless here, so drop it and fall back to a fresh sign-in. */
		blog(LOG_WARNING, "[nvs] stored credentials could not be decrypted; clearing them");
		Clear();
		return {};
	}

	const QJsonDocument document = QJsonDocument::fromJson(plaintext);
	plaintext.fill('\0');

	return document.isObject() ? document.object() : QJsonObject();
#else
	return {};
#endif
}

void NvsCredentialStore::Clear()
{
	const QString path = FilePath();

	if (!path.isEmpty() && QFile::exists(path)) {
		QFile::remove(path);
	}
}
