#ifndef TYPES_NETWORKINTERFACE_H
#define TYPES_NETWORKINTERFACE_H

#include <QDataStream>
#include <QString>
#include "enums.h"


namespace types {

struct NetworkInterface
{
    // defaults
    NetworkInterface() :
        interfaceIndex(-1),
        interfaceType(NETWORK_INTERFACE_NONE),
        trustType(NETWORK_TRUST_SECURED),
        active(false),
        requested(false),
        metric(100),
        mtu(1470),
        state(0),
        dwType(0),
        connectorPresent(false),
        endPointInterface(false)
    {}

    qint32 interfaceIndex;
    QString interfaceName;
    QString interfaceGuid;
    QString networkOrSSid;
    NETWORK_INTERACE_TYPE interfaceType;
    NETWORK_TRUST_TYPE trustType;
    bool active;
    QString friendlyName;
    bool requested;
    qint32 metric;
    QString physicalAddress;
    qint32 mtu;
    qint32 state;
    qint32 dwType;
    QString deviceName;
    bool connectorPresent;
    bool endPointInterface;

    bool operator==(const NetworkInterface &other) const
    {
        return other.interfaceIndex == interfaceIndex &&
               other.interfaceName == interfaceName &&
               other.interfaceGuid == interfaceGuid &&
               other.networkOrSSid == networkOrSSid &&
               other.interfaceType == interfaceType &&
               other.trustType == trustType &&
               other.active == active &&
               other.friendlyName == friendlyName &&
               other.requested == requested &&
               other.metric == metric &&
               other.physicalAddress == physicalAddress &&
               other.mtu == mtu &&
               other.state == state &&
               other.dwType == dwType &&
               other.deviceName == deviceName &&
               other.connectorPresent == connectorPresent &&
               other.endPointInterface == endPointInterface;
    }

    bool operator!=(const NetworkInterface &other) const
    {
        return !(*this == other);
    }

    friend QDataStream& operator <<(QDataStream &stream, const NetworkInterface &o)
    {
        stream << versionForSerialization_;
        stream << o.interfaceIndex << o.interfaceName << o.interfaceGuid << o.networkOrSSid << o.interfaceType << o.trustType << o.active <<
                  o.friendlyName << o.requested << o.metric << o.physicalAddress << o.mtu << o.state << o.dwType << o.deviceName <<
                  o.connectorPresent << o.endPointInterface;
        return stream;
    }
    friend QDataStream& operator >>(QDataStream &stream, NetworkInterface &o)
    {
        quint32 version;
        stream >> version;
        Q_ASSERT(version == versionForSerialization_);
        if (version > versionForSerialization_)
        {
            return stream;
        }
        stream >> o.interfaceIndex >> o.interfaceName >> o.interfaceGuid >> o.networkOrSSid >> o.interfaceType >> o.trustType >> o.active >>
                  o.friendlyName >> o.requested >> o.metric >> o.physicalAddress >> o.mtu >> o.state >> o.dwType >> o.deviceName >>
                  o.connectorPresent >> o.endPointInterface;
        return stream;
    }

private:
    static constexpr quint32 versionForSerialization_ = 1;

};


} // types namespace

#endif // TYPES_NETWORKINTERFACE_H
