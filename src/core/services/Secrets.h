// API keys in the system's credential store: Windows Credential Manager, or the Secret Service (GNOME Keyring,
// KWallet) on Linux. Never in the settings file or the library database.
#pragma once

#include <QString>

namespace core::secrets {

// Names are stored as "FreeReader/<name>" ("FreeReader/gemini-api-key").
QString read(const QString& name);
bool write(const QString& name, const QString& value, QString* error = nullptr);
bool remove(const QString& name);
// Tests: keep keys in memory only, never touching the real store.
void useMemoryStore(bool on);
// False when this system has no credential store (some Linux setups): keys then last until the app closes.
bool available();

}  // namespace core::secrets
