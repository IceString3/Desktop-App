#ifndef NETWORKOPTIONSSHARED_H
#define NETWORKOPTIONSSHARED_H

#include <QList>
#include <QString>
#include <types/networkinterface.h>

namespace NetworkOptionsShared
{
    types::NetworkInterface networkInterfaceByName(QString networkOrSsid);
    const char *trustTypeToString(NETWORK_TRUST_TYPE type);
}

#endif // NETWORKOPTIONSSHARED_H