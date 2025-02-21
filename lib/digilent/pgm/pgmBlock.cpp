#include "pgmBlock.h"

PgmBlock::PgmBlock()
{

}

//Equals operator.  This function  compares two PgmBlocks for equality by comparing their address fields.
bool PgmBlock::operator ==(const PgmBlock &other){
    return other.address == address;
}

//Less operator.  This function compares two PgmBlocks by their address properties.
bool PgmBlock::operator <(const PgmBlock &other) const {
    return this->address < other.address;
}
