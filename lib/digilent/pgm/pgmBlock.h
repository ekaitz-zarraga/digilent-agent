#ifndef PGMBLOCK_H
#define PGMBLOCK_H

#include <QByteArray>

class PgmBlock
{
public:
    PgmBlock();
    bool operator ==(const PgmBlock &other);
    bool operator <(const PgmBlock &other) const;

    unsigned int address;
    QByteArray data;

};

#endif // PGMBLOCK_H
