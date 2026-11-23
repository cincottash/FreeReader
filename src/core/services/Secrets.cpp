#include "Secrets.h"

#include <QHash>
#include <QMutex>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#elif defined(Q_OS_UNIX) && !defined(Q_OS_ANDROID)
#include <QLibrary>
#endif
#ifdef Q_OS_ANDROID
#include <QDir>
#include <QFile>
#include <QJniEnvironment>
#include <QJniObject>
#include <QSaveFile>
#include <QStandardPaths>
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

#ifdef Q_OS_ANDROID
// Android: the value is encrypted (AES-GCM) with a key that lives in the Android Keystore and never leaves it;
// only the encrypted bytes are kept, in the app's own private folder.
const char* const kKeyAlias = "FreeReaderSecrets";

QString secretFile(const QString& name)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/secrets");
    QDir().mkpath(dir);
    QString safe = name;
    safe.replace(QLatin1Char('/'), QLatin1Char('_'));
    return dir + QLatin1Char('/') + safe + QStringLiteral(".bin");
}

bool clearJavaException()
{
    QJniEnvironment env;
    return env.checkAndClearExceptions(QJniEnvironment::OutputMode::Silent);
}

// The Keystore's AES key, made on first use.
QJniObject keystoreKey()
{
    QJniObject alias = QJniObject::fromString(QString::fromLatin1(kKeyAlias));
    QJniObject store = QJniObject::callStaticObjectMethod("java/security/KeyStore", "getInstance", "(Ljava/lang/String;)Ljava/security/KeyStore;",
                                                          QJniObject::fromString(QStringLiteral("AndroidKeyStore")).object<jstring>());
    if (clearJavaException() || !store.isValid()) return {};
    store.callMethod<void>("load", "(Ljava/security/KeyStore$LoadStoreParameter;)V", nullptr);
    if (clearJavaException()) return {};
    const jboolean has = store.callMethod<jboolean>("containsAlias", "(Ljava/lang/String;)Z", alias.object<jstring>());
    if (clearJavaException()) return {};
    if (!has) {
        QJniEnvironment env;
        // new KeyGenParameterSpec.Builder(alias, PURPOSE_ENCRYPT | PURPOSE_DECRYPT).setBlockModes("GCM")
        //     .setEncryptionPaddings("NoPadding").build()
        QJniObject builder("android/security/keystore/KeyGenParameterSpec$Builder", "(Ljava/lang/String;I)V", alias.object<jstring>(), jint(3));
        auto strings = [&env](const char* s) {
            jobjectArray a = env->NewObjectArray(1, env->FindClass("java/lang/String"), nullptr);
            env->SetObjectArrayElement(a, 0, QJniObject::fromString(QString::fromLatin1(s)).object<jstring>());
            return a;
        };
        jobjectArray modes = strings("GCM"), paddings = strings("NoPadding");
        builder.callObjectMethod("setBlockModes", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;", modes);
        builder.callObjectMethod("setEncryptionPaddings", "([Ljava/lang/String;)Landroid/security/keystore/KeyGenParameterSpec$Builder;", paddings);
        env->DeleteLocalRef(modes);
        env->DeleteLocalRef(paddings);
        QJniObject spec = builder.callObjectMethod("build", "()Landroid/security/keystore/KeyGenParameterSpec;");
        QJniObject generator = QJniObject::callStaticObjectMethod("javax/crypto/KeyGenerator", "getInstance",
                                                                  "(Ljava/lang/String;Ljava/lang/String;)Ljavax/crypto/KeyGenerator;",
                                                                  QJniObject::fromString(QStringLiteral("AES")).object<jstring>(),
                                                                  QJniObject::fromString(QStringLiteral("AndroidKeyStore")).object<jstring>());
        if (clearJavaException() || !generator.isValid() || !spec.isValid()) return {};
        generator.callMethod<void>("init", "(Ljava/security/spec/AlgorithmParameterSpec;)V", spec.object());
        generator.callObjectMethod("generateKey", "()Ljavax/crypto/SecretKey;");
        if (clearJavaException()) return {};
    }
    QJniObject key = store.callObjectMethod("getKey", "(Ljava/lang/String;[C)Ljava/security/Key;", alias.object<jstring>(), nullptr);
    if (clearJavaException()) return {};
    return key;
}

QByteArray toBytes(const QJniObject& array)
{
    if (!array.isValid()) return {};
    QJniEnvironment env;
    auto a = static_cast<jbyteArray>(array.object());
    const jsize n = env->GetArrayLength(a);
    QByteArray out(n, Qt::Uninitialized);
    env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(out.data()));
    return out;
}

jbyteArray toJava(QJniEnvironment& env, const QByteArray& bytes)
{
    jbyteArray a = env->NewByteArray(jsize(bytes.size()));
    env->SetByteArrayRegion(a, 0, jsize(bytes.size()), reinterpret_cast<const jbyte*>(bytes.constData()));
    return a;
}

QJniObject cipher()
{
    return QJniObject::callStaticObjectMethod("javax/crypto/Cipher", "getInstance", "(Ljava/lang/String;)Ljavax/crypto/Cipher;",
                                              QJniObject::fromString(QStringLiteral("AES/GCM/NoPadding")).object<jstring>());
}

// The file: a version byte, the 12-byte IV, then the encrypted value with its tag.
bool androidWrite(const QString& name, const QString& value)
{
    const QJniObject key = keystoreKey();
    QJniObject c = cipher();
    if (!key.isValid() || !c.isValid()) return false;
    c.callMethod<void>("init", "(ILjava/security/Key;)V", jint(1), key.object());   // ENCRYPT_MODE
    if (clearJavaException()) return false;
    QJniEnvironment env;
    jbyteArray plain = toJava(env, value.toUtf8());
    const QByteArray sealed = toBytes(c.callObjectMethod("doFinal", "([B)[B", plain));
    env->DeleteLocalRef(plain);
    const QByteArray iv = toBytes(c.callObjectMethod("getIV", "()[B"));
    if (clearJavaException() || sealed.isEmpty() || iv.size() != 12) return false;
    QSaveFile f(secretFile(name));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QByteArray(1, '\x01') + iv + sealed);
    return f.commit();
}

QString androidRead(const QString& name)
{
    QFile f(secretFile(name));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = f.readAll();
    if (data.size() < 1 + 12 + 16 || data.at(0) != '\x01') return {};
    const QJniObject key = keystoreKey();
    QJniObject c = cipher();
    if (!key.isValid() || !c.isValid()) return {};
    QJniEnvironment env;
    jbyteArray iv = toJava(env, data.mid(1, 12));
    QJniObject spec("javax/crypto/spec/GCMParameterSpec", "(I[B)V", jint(128), iv);
    env->DeleteLocalRef(iv);
    c.callMethod<void>("init", "(ILjava/security/Key;Ljava/security/spec/AlgorithmParameterSpec;)V", jint(2), key.object(), spec.object());   // DECRYPT_MODE
    if (clearJavaException()) return {};
    jbyteArray sealed = toJava(env, data.mid(13));
    const QByteArray plain = toBytes(c.callObjectMethod("doFinal", "([B)[B", sealed));
    env->DeleteLocalRef(sealed);
    if (clearJavaException()) return {};
    return QString::fromUtf8(plain);
}
#endif

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS) && !defined(Q_OS_ANDROID)
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
#elif defined(Q_OS_ANDROID)
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
#elif defined(Q_OS_ANDROID)
    return androidRead(name).trimmed();
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
#elif defined(Q_OS_ANDROID)
    if (!androidWrite(name, value)) {
        memory().insert(name, value);   // for this session only
        if (error) *error = QStringLiteral("The phone's key store refused it, so the key is kept until the app closes.");
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
#elif defined(Q_OS_ANDROID)
    QFile::remove(secretFile(name));
    return true;
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
