#ifndef SQLITEDROPTRIGGER_H
#define SQLITEDROPTRIGGER_H

#include "sqlitequery.h"
#include <QString>

class API_EXPORT SqliteDropTrigger : public SqliteQuery
{
    public:
        SqliteDropTrigger();
        SqliteDropTrigger(bool ifExistsKw, const QString& name1, const QString& name2);

        bool ifExistsKw = false;
        QString database = QString::null;
        QString trigger = QString::null;

    protected:
        QStringList getDatabasesInStatement();
        TokenList getDatabaseTokensInStatement();
        QList<FullObject> getFullObjectsInStatement();
};

typedef QSharedPointer<SqliteDropTrigger> SqliteDropTriggerPtr;

#endif // SQLITEDROPTRIGGER_H