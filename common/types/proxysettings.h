#ifndef TYPES_PROXYSETTINGS_H
#define TYPES_PROXYSETTINGS_H

#include <QSettings>
#include <QString>
#include <QNetworkProxy>
#include "enums.h"

namespace types {

class ProxySettings
{
public:
    ProxySettings();
    ProxySettings(PROXY_OPTION option, const QString &address, uint port, const QString &password, const QString &username);

    PROXY_OPTION option() const;
    void setOption(PROXY_OPTION option);

    QString address() const;
    void setAddress(const QString &address);

    uint getPort() const;
    void setPort(const uint &value);

    QString getUsername() const;
    void setUsername(const QString &username);

    QString getPassword() const;
    void setPassword(const QString &password);

    void debugToLog();

    QNetworkProxy getNetworkProxy() const;
    bool isProxyEnabled() const;

    bool operator==(const ProxySettings &other) const {
        return option_ == other.option_ && address_ == other.address_ && port_ == other.port_
               && username_ == other.username_ && password_ == other.password_;
    }
    bool operator!=(const ProxySettings &other) const {
        return !(*this == other);
    }

    friend QDataStream& operator <<(QDataStream &stream, const ProxySettings &o)
    {
        stream << versionForSerialization_;
        stream << o.option_ << o.address_ << o.port_ << o.username_ << o.password_;
        return stream;
    }
    friend QDataStream& operator >>(QDataStream &stream, ProxySettings &o)
    {
        quint32 version;
        stream >> version;
        Q_ASSERT(version == versionForSerialization_);
        if (version > versionForSerialization_)
        {
            return stream;
        }
        stream >> o.option_ >> o.address_ >> o.port_ >> o.username_ >> o.password_;
        return stream;
    }

private:
    PROXY_OPTION option_;
    QString address_;
    uint port_;
    QString username_;
    QString password_;

    static constexpr quint32 versionForSerialization_ = 1;
};
} //namespace types

#endif // TYPES_PROXYSETTINGS_H
