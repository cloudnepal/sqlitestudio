#include "sqlitereindex.h"
#include "sqlitequerytype.h"

SqliteReindex::SqliteReindex()
{
    queryType = SqliteQueryType::Reindex;
}

SqliteReindex::SqliteReindex(const QString& name1, const QString& name2)
    : SqliteReindex()
{
    if (!name2.isNull())
    {
        database = name1;
        table = name2;
    }
    else
        table = name1;
}

QStringList SqliteReindex::getTablesInStatement()
{
    return getStrListFromValue(table);
}

QStringList SqliteReindex::getDatabasesInStatement()
{
    return getStrListFromValue(database);
}

TokenList SqliteReindex::getTableTokensInStatement()
{
    return getObjectTokenListFromNmDbnm();
}

TokenList SqliteReindex::getDatabaseTokensInStatement()
{
    return getDbTokenListFromNmDbnm();
}

QList<SqliteStatement::FullObject> SqliteReindex::getFullObjectsInStatement()
{
    QList<FullObject> result;

    // Table object
    FullObject fullObj = getFullObjectFromNmDbnm(FullObject::TABLE);

    if (fullObj.isValid())
        result << fullObj;

    // Db object
    fullObj = getFirstDbFullObject();
    if (fullObj.isValid())
        result << fullObj;

    return result;
}