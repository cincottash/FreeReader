#include "Secrets.h"

#include <QHash>
#include <QMutex>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_UNIX)
#include <QLibrary>
#endif

namespace core::secrets {

namespace {

QMutex g_lock;
bool g_memoryOnly = false;
QHash<QString, QString>& memory()
{
    static QHash<QString, QString> m;
    return m;
}

QString target(const QString& name) { return QStringLiteral(APP_NAME "/") + name; }

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
// libsecret is loaded when needed, so the app runs (keys kept for the session) where it isn't installed.
struct SecretSchemaAttribute {
    const char* name;
    int type;   // SECRET_SCHEMA_ATTRIBUTE_STRING = 0
};
struct SecretSchema {
    const char* name;
    int flags;
    SecretSchemaAttribute attributes[32];
    int reserved;
    void* reserved1;
    void* reserved2;
    void* reserved3;
    void* reserved4;
    void* reserved5;
    void* reserved6;
    void* reserved7;
};
using StoreFn = int (*)(const SecretSchema*, const char*, const char*, const char*, void*, void**, ...);
using LookupFn = char* (*)(const SecretSchema*, void*, void**, ...);
using ClearFn = int (*)(const SecretSchema*, void*, void**, ...);
using FreeFn = void (*)(char*);

struct LibSecret {
    StoreFn store = nullptr;
    LookupFn lookup = nullptr;
    ClearFn clear = nullptr;
    FreeFn free = nullptr;
    bool ok = false;
    LibSecret()
    {
        static QLibrary lib(QStringLiteral("libsecret-1"), 0);
        if (!lib.load()) return;
        store = reinterpret_cast<StoreFn>(lib.resolve("secret_password_store_sync"));
        lookup = reinterpret_cast<LookupFn>(lib.resolve("secret_password_lookup_sync"));
        clear = reinterpret_cast<ClearFn>(lib.resolve("secret_password_clear_sync"));
        free = reinterpret_cast<FreeFn>(lib.resolve("secret_password_free"));
        ok = store && lookup && clear && free;
    }
};
LibSecret& libsecret()
{
    static LibSecret s;
    return s;
}
const SecretSchema* schema()
{
    static const SecretSchema s = {"org.cincottash.FreeReader", 0, {{"name", 0}, {nullptr, 0}}, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    return &s;
}
#endif

}  // namespace

void useMemoryStore(bool on)
{
    QMutexLocker l(&g_lock);
    g_memoryOnly = on;
    memory().clear();
}

bool available()
{
    if (g_memoryOnly) return true;
#ifdef Q_OS_WIN
    return true;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return libsecret().ok;
#else
    return false;
#endif
}

QString read(const QString& name)
{
    QMutexLocker l(&g_lock);
    if (g_memoryOnly || memory().contains(name)) return memory().value(name);
#ifdef Q_OS_WIN
    PCREDENTIALW cred = nullptr;
    const std::wstring t = target(name).toStdWString();
    if (!CredReadW(t.c_str(), CRED_TYPE_GENERIC, 0, &cred)) return {};
    QString value;
    // cmdkey and Credential Manager store the password as UTF-16; this app does too.
    if (cred->CredentialBlobSize >= 2 && cred->CredentialBlobSize % 2 == 0)
        value = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(cred->CredentialBlob), int(cred->CredentialBlobSize / 2));
    else if (cred->CredentialBlobSize > 0)
        value = QString::fromUtf8(reinterpret_cast<const char*>(cred->CredentialBlob), int(cred->CredentialBlobSize));
    CredFree(cred);
    return value.trimmed();
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (!libsecret().ok) return {};
    const QByteArray n = name.toUtf8();
    void* err = nullptr;
    char* pw = libsecret().lookup(schema(), nullptr, &err, "name", n.constData(), nullptr);
    if (!pw) return {};
    const QString value = QString::fromUtf8(pw);
    libsecret().free(pw);
    return value;
#else
    return {};
#endif
}

bool write(const QString& name, const QString& value, QString* error)
{
    QMutexLocker l(&g_lock);
    if (g_memoryOnly) {
        memory().insert(name, value);
        return true;
    }
#ifdef Q_OS_WIN
    const std::wstring t = target(name).toStdWString();
    const std::wstring v = value.toStdWString();
    const std::wstring user = QStringLiteral(APP_NAME).toStdWString();
    CREDENTIALW c = {};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = const_cast<wchar_t*>(t.c_str());
    c.CredentialBlobSize = DWORD(v.size() * sizeof(wchar_t));
    c.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<wchar_t*>(v.c_str()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    c.UserName = const_cast<wchar_t*>(user.c_str());
    if (!CredWriteW(&c, 0)) {
        if (error) *error = QStringLiteral("Windows Credential Manager refused it (error %1).").arg(GetLastError());
        return false;
    }
    memory().remove(name);
    return true;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (!libsecret().ok) {
        memory().insert(name, value);   // for this session only
        if (error) *error = QStringLiteral("No credential store was found, so the key is kept until the app closes.");
        return false;
    }
    const QByteArray n = name.toUtf8(), v = value.toUtf8(), label = target(name).toUtf8();
    void* err = nullptr;
    if (!libsecret().store(schema(), "default", label.constData(), v.constData(), nullptr, &err, "name", n.constData(), nullptr)) {
        memory().insert(name, value);
        if (error) *error = QStringLiteral("The system's key store refused it, so the key is kept until the app closes.");
        return false;
    }
    return true;
#else
    memory().insert(name, value);
    if (error) *error = QStringLiteral("No credential store on this system, so the key is kept until the app closes.");
    return false;
#endif
}

bool remove(const QString& name)
{
    QMutexLocker l(&g_lock);
    memory().remove(name);
    if (g_memoryOnly) return true;
#ifdef Q_OS_WIN
    const std::wstring t = target(name).toStdWString();
    return CredDeleteW(t.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
#elif defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (!libsecret().ok) return true;
    const QByteArray n = name.toUtf8();
    void* err = nullptr;
    libsecret().clear(schema(), nullptr, &err, "name", n.constData(), nullptr);
    return true;
#else
    return true;
#endif
}

}  // namespace core::secrets
