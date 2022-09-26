#pragma once

#include "baserequest.h"

namespace server_api {

class SyncRobertRequest : public BaseRequest
{
    Q_OBJECT
public:
    explicit SyncRobertRequest(QObject *parent,  const QString &hostname, const QString &authHash);

    QUrl url() const override;
    QString name() const override;
    void handle(const QByteArray &arr) override;

private:
    QString authHash_;
};

} // namespace server_api {

