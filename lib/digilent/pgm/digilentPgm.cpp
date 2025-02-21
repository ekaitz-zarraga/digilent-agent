#include "digilentPgm.h"

#include <QTime>
#include <QFile>

#include "../qtSerial/serial.h"

QString DigilentPgm::argHexFile = QString();
QString DigilentPgm::argComPort = QString();
QString DigilentPgm::argBoardName = QString();

bool DigilentPgm::fDigilentBootloader   = false;
char DigilentPgm::seqNbrSend            = 0;
char DigilentPgm::seqNbrRec             = 0;
int  DigilentPgm::pgmBlkSize             = cbSTK500Blk;
int DigilentPgm::uartBaud               = 115200;

int  DigilentPgm::vendorID              = 0;
int  DigilentPgm::prodID                = 0;
unsigned int  DigilentPgm::devID                 = 0;
unsigned int  DigilentPgm::bootloaderVersion     = 0;
int  DigilentPgm::capabilities          = 0;
int  DigilentPgm::flashRowSize          = 0;
int  DigilentPgm::flashPageSize         = 0;

int DigilentPgm::progress = 0;
DigilentPgm::DigilentPgm()
{
    this->progress = -1;
}

bool DigilentPgm::programByPort(QString hexPath, QString portName){
    if(this->port.open(portName, 115200))
    {
        QByteArray resp = signOn();
        if(!resp.isNull() && resp[1] == STATUS_CMD_OK)
        {
            if(!isDigilentBootloader()){
                qStdOut() << "Bootloader only supports STK500v2" << endl;
            }
        }
        bool status = programActivePort(hexPath);
        this->port.close();
        return status;

    } else {
        return false;
    }
}

bool DigilentPgm::programByBoardName(QString hexPath, QString boardName) {
    //Find and open the port with the specified boardName attached
    if(findSerialPort(boardName))
    {
        return programActivePort(hexPath);
    } else {
        qStdOut() << "Can't find board: " << argBoardName << endl;
        return false;
    }
}

bool DigilentPgm::programActivePort(QString hexPath) {
    //Initialize variables
    QVector<PgmBlock> hexList;
    this->seqNbrSend = 0;

    //Process hex list
    hexList = processHexFile(hexPath, this->pgmBlkSize, 0xFF);
    if(hexList.length() <= 0) {
        qStdOut() << "Unable to process Hex File" << endl;
        return false;
    } else {
        if(stk500v2Pgm(hexList)) {
            return true;
        } else {
            qStdOut() << "Unable to program Hex File" << endl;
            return false;
        }
    }
}

bool DigilentPgm::findSerialPort(QString strBoardName) {
    //Refresh the system port information
    this->serialPortInfo = Serial::getSerialPortInfo();

    for(int i=0; i<this->serialPortInfo.length(); i++)
    {
        qStdOut() << "Checking" <<this->serialPortInfo[i].portName() << endl;

        //Open serial port
        if(this->port.open(this->serialPortInfo[i], this->uartBaud)) {
            //Reset board
            this->port.assertReset();
            //Sign on
            QByteArray resp = signOn();
            if(!resp.isNull() && resp[1] == STATUS_CMD_OK)
            {
                //Check hardware version
                QByteArray hwVer = getParam(PARAM_HW_VER);
                if(!hwVer.isNull() && hwVer[1] == STATUS_CMD_OK && hwVer[2] == VEND_DIGILENT) {
                    //Check software version
                    QByteArray swVer = getParam(PARAM_SW_MAJOR);
                    if(!swVer.isNull() && resp[1] == STATUS_CMD_OK && (unsigned char)resp[2] >= 2){
                        //Check board name
                        QString boardName = getBoardName();
                        if(boardName == strBoardName){
                            //Board name matches target
                            qStdOut() << "Found COM port:" << this->serialPortInfo[i].portName() << "for board:" << boardName << endl;
                            isDigilentBootloader();
                            return true;
                        }
                    }
                }
            } else {
                //Sign on failed
                qStdOut() << "These aren't the boards we're looking for.  You can go about your business.  Move along. " << endl;
                qStdOut() << "COM port: " << this->port.getName() << " is not: " << strBoardName << endl;
                this->port.close();
            }
        } else {
             //qStdOut() << "Failed to open" << this->serialPortInfo[i].portName() << endl;
             this->port.close();
        }
    }
    return false;
}


QByteArray DigilentPgm::signOn() {
    //Make sure the port is open
    if(!this->port.isOpen()) {
        return NULL;
    }

    //Clear input buffer
    this->port.read();

    //Send sign on command
    QByteArray data;
    data.append(CMD_SIGN_ON);

    if(!sendMessage(data)) {
        qStdOut() << "Failed to write 'SignOn' command" << endl;
        return NULL;
    }

    //Wait for response, then return it
    QByteArray x = waitForResponse();
    return x;
}

QByteArray DigilentPgm::createMsg(QByteArray body){
    QByteArray msg;
    msg[0] = 0x1B;
    msg[1] = 0x00;
    msg[2] = 0x00;
    msg[3] = 0x00;
    msg[4] = 0x0E;

    //Copy body into msg
    for(int i=0; i< body.length(); i++) {
        msg.append(body[i]);
    }
    //Add 0x00 checksum byte
    msg.append((char)0x00);

    msg[1] = this->seqNbrSend++;
    msg[2] = (char)(body.length() >> 8);
    msg[3] = (char)(body.length() & 0xFF);

    msg[msg.length() - 1] = xorBuffer(msg);
    return msg;
}

bool DigilentPgm::sendMessage(QByteArray body) {
    QByteArray msg = createMsg(body);
    if(msg.isNull()){
        return false;
    }
    return this->port.write(msg);
}


//XOR all bytes in the buffer except the last and return the value (checksum).
char DigilentPgm::xorBuffer(QByteArray buffer) {
    int checksum = buffer[buffer.length() - 1];
    for (int i = 0; i < buffer.length() - 1; i++) {
        checksum = checksum ^ buffer[i];
    }
    return checksum;
}

//Wait for a response from the device for up to the pre defined timeout.  This function returns the response body if one is received or NULL otherwise.
QByteArray DigilentPgm::waitForResponse() {
    QTime startTime = QTime::currentTime();
    QTime doneTime = startTime.addMSecs(this->timeOutSTK500Read);

    int cbReadGet = this->cbMsgOverhead + 1;
    int cbReadGot = 0;
    QByteArray retMsg;
    retMsg.resize(cbReadGet);

    //Read until a full packet is received or a timeout
    while(cbReadGet > 0){

        //Read first part of packet
        if(this->port.bytesAvailable() >= cbReadGet) {
            QByteArray dataRead = this->port.read(cbReadGet);
            retMsg.replace(cbReadGot, cbReadGet, dataRead);
            int cb = ((((int) retMsg[2])) << 8 | (int) retMsg[3]) + cbMsgOverhead;
            cbReadGot += cbReadGet;
            cbReadGet = cb - cbReadGot;

            if(cb == cbMsgOverhead)
            {
                return(NULL);
            }
            else if(cb > retMsg.length())
            {
                retMsg.resize(cb);
            }
        } else if(QTime::currentTime() >= doneTime) {
            //Check timeout
            qStdOut() << "Timeout waiting for response" << endl;
            return NULL;
        }
    }

    //Done reading packet, check checksum
    if(xorBuffer(retMsg) != 0){
        qStdOut() << "Checksum failed" << endl;
    }

    //Return body
    return retMsg.mid(this->iMsgBody, cbReadGot);
}

QByteArray DigilentPgm::getParam(char param) {
    QByteArray body;
    body.append(CMD_GET_PARAMETER);
    body.append(param);

    if(!sendMessage(body)){
        return NULL;
    }
    return waitForResponse();
}

//Get the board name from the device
QString DigilentPgm::getBoardName() {
    QByteArray body;
    body.append(CMD_GET_BOARD_NAME);

    if(!sendMessage(body)) {
        return NULL;
    }

    QByteArray resp = waitForResponse();
    if(!resp.isNull() && resp[1] == STATUS_CMD_OK) {
        //Trim 2 header bytes and 2 footer bytes (cmd, status | null, checksum)
        QString boardName = QString(resp.mid(2, body.length()-4));
        return boardName;
    } else {
        return NULL;
    }
}

bool DigilentPgm::isDigilentBootloader() {
    //Check hardware version
    QByteArray hwVer = getParam(PARAM_HW_VER);
    if(!hwVer.isNull() && hwVer[1] == STATUS_CMD_OK && hwVer[2] == VEND_DIGILENT) {
        //Check software version
        QByteArray swVer = getParam(PARAM_SW_MAJOR);
        if(!swVer.isNull() && swVer[1] == STATUS_CMD_OK && (unsigned char)swVer[2] >= 2){
            //Check bootloader and board name
            QString boardName = getBoardName();
            if(boardName != NULL && getBootloaderInfo()) {
                //Everythign checks out
                int curBaud = uartBaud;
                QVector<int> bauds = getBaudRates();

                fDigilentBootloader = true;
                qStdOut() << "Extended Digilent Bootload Found" << endl;
                qStdOut() << "\nBoard Name: " << boardName << endl;

                switch(vendorID) {
                    case 0x0001:
                        qStdOut() << "Vendor Digilent" << endl;
                    break;
                    case 0x0002:
                        qStdOut() << "Vendor Microchip" << endl;
                    break;
                    case 0x0003:
                        qStdOut() << "Vendor Fubarino" << endl;
                    break;
                    case 0x0004:
                        qStdOut() << "Vendor SchmalzHausLLC" << endl;
                    break;
                    case 0x0005:
                        qStdOut() << "Vendor Olimex" << endl;
                    break;
                    case 0x0006:
                        qStdOut() << "Vendor Element14" << endl;
                    break;
                    case 0x8000:
                        qStdOut() << "Vendor Experimental" << endl;
                    break;
                default:
                    qStdOut() << "Unknown Vendor: " << vendorID << endl;
                    break;
                }
                qStdOut() << "Bootloader Version" << toHexString(bootloaderVersion) << endl;
                qStdOut() << "Capabilities:" << toHexString(capabilities) << endl;
                qStdOut() << "Device ID:" << toHexString(devID);
                qStdOut() << "Flash Page Size:" << flashPageSize << endl;
                qStdOut() << "Flash Row Size:" << flashRowSize << endl;

                //Find the max baud
                for(int i=0; i<bauds.length(); i++){
                    if(bauds[i] > uartBaud) {
                        uartBaud = bauds[i];
                    }
                }

                //Make the block size the row size
                pgmBlkSize = flashRowSize;

                //Change to the new baud
                QByteArray resp = this->setBaudRate(uartBaud);
                if(!resp.isNull() && resp[1] == STATUS_CMD_OK){
                    if(uartBaud != this->port.getBaudRate()) {
                        if(!this->port.setBaudRate(uartBaud)){
                            uartBaud = curBaud;
                            qStdOut() << "Resetting baud rate to " << uartBaud << endl;
                            this->port.setBaudRate(uartBaud);
                            this->port.assertReset();
                            signOn();
                        } else {
                            qStdOut() << "Baud rate set to :" << uartBaud << endl;
                        }
                    }
                }
                return true;
            } else {
                //Invalid bootloader or board name
                return false;
            }
        } else {
            //Invalid software version
            return false;
        }
    } else {
        //Invlaid hardware version
        return false;
    }
}

bool DigilentPgm::getBootloaderInfo() {
    QByteArray body;
    body.append(CMD_GET_BOOTLOADER_INFO);

    if(!sendMessage(body)){
        return false;
    }

    QByteArray resp = waitForResponse();
    if(!resp.isNull() && resp[1] == STATUS_CMD_OK && resp.length() >= 24) {
        vendorID            = byteArrayToUShort(resp.mid(2, 2));
        prodID              = byteArrayToUInt(resp.mid(4, 4));
        devID               = byteArrayToUInt(resp.mid(8, 4));
        bootloaderVersion   = byteArrayToUInt(resp.mid(12, 4));
        capabilities        = byteArrayToUInt(resp.mid(16, 4));
        flashRowSize        = byteArrayToUInt(resp.mid(20, 4));
        flashPageSize       = byteArrayToUInt(resp.mid(24, 4));
        return true;
    }
    return false;
}

QVector<int> DigilentPgm::getBaudRates() {
    QByteArray body;
    body.append(CMD_GET_BAUD_RATES);

    if(!sendMessage(body)){
        return QVector<int>();
    }

    QByteArray resp = waitForResponse();
    if(!resp.isNull() && resp[1] == STATUS_CMD_OK){
        int cBaud = byteArrayToUShort(resp.mid(2, 2));
        QVector<int> baud(cBaud);

        for(int i=0; i<cBaud; i++){
            baud[i] = byteArrayToUInt(resp.mid(4+i*4, 4));
        }
        return baud;
    }
    return QVector<int>();
}

//Set the device baud rate
QByteArray DigilentPgm::setBaudRate(int baud) {
    QByteArray body(5, 0x00);
    body[0] = CMD_SET_BAUD_RATE;
    body[1] = (baud >> 24) & 0xFF;
    body[2] = (baud >> 16) & 0xFF;
    body[3] = (baud >> 8) & 0xFF;
    body[4] = baud & 0xFF;

    if(!sendMessage(body)){
        return NULL;
    }
    return waitForResponse();
}

unsigned short DigilentPgm::byteArrayToUShort(QByteArray array) {
    if(array.length() != 2){
        return NULL;
    } else {
        unsigned short retVal = (array[0] << 8) | array[1];
        return retVal;
    }
}

unsigned int DigilentPgm::byteArrayToUInt(QByteArray array) {
    if(array.length() != 4){
        return NULL;
    } else {
        unsigned int retVal = (unsigned char)array[0] << 24 | (unsigned char)array[1] << 16 | (unsigned char)array[2] << 8 | (unsigned char)array[3];
        return retVal;
    }
}

QString DigilentPgm::toHexString(unsigned int value) {
    QString header = "0x";
    QString hexVal = QString::number(value, 16);
    return header + hexVal;
}

QVector<PgmBlock> DigilentPgm::processHexFile(QString hexFileName, int cbBlkSize, unsigned char fill) {
    QVector<PgmBlock> hexList;
    QTime stopWatch;

    qStdOut() << "\nProcessing Hex file:" << hexFileName << endl;
    stopWatch.start();

    hexList = createHexFileBlockList(hexFileName);
    if(hexList.length() > 0) {
        hexList = pageHexFileBlockList(hexList, cbBlkSize, fill);
    }

    if(hexList.length() > 0) {
        qStdOut() << "Successfully processed Hex file:" << hexFileName << endl;
        qStdOut() << "Program block size:" << cbBlkSize << "bytes" << endl;
        qStdOut() << "Program size:" << cbBlkSize * hexList.length() << "bytes" << endl;
    } else {
        qStdOut() << "Failed to process Hex file:" << hexFileName << endl;
    }

    unsigned int processTime = stopWatch.elapsed();
    qStdOut() << "Hex file processing time: " << processTime / 1000 << "s" << processTime % 1000 << "ms" << endl;

    return hexList;
}

QVector<PgmBlock> DigilentPgm::createHexFileBlockList(QString hexFileName) {
    QVector<PgmBlock> hexList;

    //Open HEX file
    QFile hexFile(hexFileName);
    if(!hexFile.open(QIODevice::ReadOnly)){
        qStdOut() << "Unable to open Hex file:" << hexFileName << endl;
        return QVector<PgmBlock>();
    }

    //Process HEX line by line
    int baseAddress = 0;
    bool fExit = false;
    bool fEndOfFile = false;

    while(!fExit && !fEndOfFile && !hexFile.atEnd()) {
        QByteArray line = hexFile.readLine();

        if(line.length() >= 9 && line[0] == ':') {
            bool ok = false;
            unsigned int cbData = line.mid(1, 2).toUInt(&ok, 16);
            if(!ok) {
                qStdOut() << "Failed to process cbData" << endl;
                fExit = true;
            }
            unsigned int address = line.mid(3, 4).toUInt(&ok, 16);
            if(!ok) {
                qStdOut() << "Failed to process address" << endl;
                fExit = true;
            }
            unsigned int record = line.mid(7, 2).toUInt(&ok, 16);
            if(!ok) {
                qStdOut() << "Failed to process record" << endl;
                fExit = true;
            }

            switch(record) {
                //Data
                case 0:
                {
                    PgmBlock pgmBlk;
                    pgmBlk.data.append(cbData);
                    pgmBlk.address = baseAddress + address;

                    for(unsigned int i=0; i<cbData; i++) {
                        int iCur = 9 + (i * 2);
                        pgmBlk.data[i] = line.mid(iCur, 2).toUInt(0, 16);
                    }
                    hexList.append(pgmBlk);
                    break;
                }

                //End of file
                case 1:
                    fEndOfFile = true;
                    break;

                 //Extended segment address
                case 2:
                    qStdOut() << "Unsupported Segment Offset:" << line.mid(9, 4) << endl;
                    fExit = true;
                    break;

                //Start segment address
                case 3:
                    qStdOut() << "Unsupported CS:IP Start Address:" << line.mid(9, 8) << endl;
                    fExit = true;
                    break;

                //Extened linear address
                case 4:
                {
                    unsigned int extendedAddress = line.mid(9, 4).toUInt(0, 16);
                    baseAddress = ((extendedAddress << 16) & 0xFFFF0000);
                    break;
                }

                //Start linear address
                case 5:

                    baseAddress = line.mid(9, 8).toUInt(0, 16);
                    break;

                default:
                qStdOut() << "Unknown record value:" << line.mid(7, 2) << endl;
                    fExit = true;
                    break;
            }
        } else {
            qStdOut() << "Error parsing" << line << endl;
            fExit = true;
        }
    }

    hexFile.close();

    if(!fEndOfFile || fExit) {
        qStdOut() << "Hex file" << hexFileName << "did not terminate properly" << endl;
        return QVector<PgmBlock>();
    } else if(hexList.length() > 0){
        std::sort(hexList.begin(), hexList.end());
    }
    return hexList;
}


QVector<PgmBlock> DigilentPgm::pageHexFileBlockList(QVector<PgmBlock> hexList, int cbBlock, unsigned char hexFill) {
    QVector<PgmBlock> blkList;
    PgmBlock *pgmBlkCur = NULL;
    int mask = ~(cbBlock -1);
    int iBlk;
    int iBlkCur = cbBlock;
    int curAddr = 0;

    //Loop over all program blocks
    for(int i=0; i<hexList.length(); i++) {
        iBlk = 0;
        if(curAddr > hexList[i].address) {
            qStdOut() << "Overlapping addresses from :" << toHexString(hexList[i].address) << "to" << toHexString(curAddr) << endl;

            if(curAddr <= hexList[i].address + hexList[i].data.length()) {
                iBlk = curAddr - hexList[i].address;
            } else {
                iBlk = hexList[i].data.length();
            }
        }

        //Completely empty this program block
        while(iBlk < hexList[i].data.length()) {
            //Get a new block if the current one is filled
            if(iBlkCur == cbBlock) {
                if(pgmBlkCur != NULL) {
                    blkList.append(*pgmBlkCur);
                }
                pgmBlkCur = new PgmBlock();
                curAddr = ((hexList[i].address + iBlk) & mask);
                pgmBlkCur->address = curAddr;
                pgmBlkCur->data = QByteArray().append(cbBlock);
                iBlkCur = 0;
            }

            //Fill to the address or end of block
            while(curAddr < hexList[i].address && iBlkCur < cbBlock) {
                pgmBlkCur->data[iBlkCur] = hexFill;
                curAddr++;
                iBlkCur++;
            }

            //Fill with data until end of one of the blocks
            while(iBlk < hexList[i].data.length() && iBlkCur < cbBlock){
                pgmBlkCur->data[iBlkCur] = hexList[i].data[iBlk];
                curAddr++;
                iBlkCur++;
                iBlk++;
            }
        }
    }

    //The last block may not have been filled
    if(pgmBlkCur != NULL) {
        if(iBlkCur < cbBlock) {
            //Fill to the end of the block with 0xFFs
            while(iBlkCur < cbBlock) {
                pgmBlkCur->data[iBlkCur] = hexFill;
                curAddr++;
                iBlkCur++;
            }
        }
        blkList.append(*pgmBlkCur);
    }
    return blkList;
}


bool DigilentPgm::stk500v2Pgm(QVector<PgmBlock> hexList) {
    QByteArray resp;
    bool fPass = true;

    resp = signOn();
    if(!resp.isNull() && resp[1] == STATUS_CMD_OK){
        resp = enterPgm();
        if(!resp.isNull() && resp[1] == STATUS_CMD_OK) {
            unsigned int curAddr = 0;
            unsigned int iBlock = 0;
            QTime stopWatch;

            qStdOut() << "\nStarting Programming: ";
            stopWatch.start();

            for(int i=0; i<hexList.length(); i++) {
                //Update progress
                int hll = hexList.length();
                this->progress = (i*100)/hexList.length();
                qDebug() << "PROGRESS: " << i << " of " << hexList.length() << "=" << this->progress;

                if(this->progress > 100 || this->progress < 0) {
                    qDebug () << "goofy progress value";
                }

                if(curAddr != hexList[i].address) {
                    resp = loadAddress(hexList[i].address);
                    if(!resp.isNull() && resp[1] == STATUS_CMD_OK){
                        curAddr = hexList[i].address;
                    } else {
                        qStdOut() << "Failed to load program address at:" << toHexString(hexList[i].address) << endl;
                        fPass = false;
                        break;
                    }
                }
                resp = pgmData(hexList[i].data);
                if(!resp.isNull() && resp[1] == STATUS_CMD_OK) {
                    unsigned int prtBlock = iBlock %10;
                    qStdOut() << prtBlock;

                    if(prtBlock == 9) {
                        qStdOut() << " ";
                    }

                    iBlock++;
                    if((iBlock % 50) == 0){
                        qStdOut() << "\n                       " << endl;
                    }
                } else {
                    qStdOut() << "Failed to program data at address:" << toHexString(curAddr) << endl;
                    fPass = false;
                    break;
                }
                curAddr += hexList[i].data.length();
            }
            unsigned int programTime = stopWatch.elapsed();
            qStdOut() << "\n" << "Program time:" << programTime / 1000 << "s" << programTime % 1000 << "ms" << endl;

            //If we passed programming do verification
            if(!fDigilentBootloader && fPass) {
                unsigned int cErr = 0;

                curAddr = 0;
                iBlock = 0;

                qStdOut() << "Starting verification: " << endl;
                stopWatch.restart();

                for(int i=0; i<hexList.length(); i++) {
                    if(curAddr != hexList[i].address){
                        resp = loadAddress(hexList[i].address);
                        if(!resp.isNull() && resp[1] == STATUS_CMD_OK) {
                            curAddr = hexList[i].address;
                        } else {
                            qStdOut() << "Failed to load program address at" << toHexString(hexList[i].address) << endl;
                            fPass = false;
                            break;
                        }
                    }

                    resp = readData(pgmBlkSize);
                    if(!resp.isNull() && resp[1] == STATUS_CMD_OK) {
                        if(hexList[i].data == resp.mid(2, pgmBlkSize)) {

                            unsigned int prtBlock = iBlock %10;
                            qStdOut() << prtBlock << endl;

                            if(prtBlock == 9) {
                                qStdOut() << " " << endl;
                            }

                            iBlock++;
                            if((iBlock % 50) == 0){
                                qStdOut() << "\n                       " << endl;
                            }
                        } else {
                            qStdOut() << "!\n" << "Correct Data:" << hexList[i].data << endl;
                            qStdOut() << "Bad Data:" << resp.mid(2, pgmBlkSize+2) << endl;
                            fPass = false;
                            cErr++;
                        }
                    } else {
                        qStdOut() << "Failed to read program data at address: " << toHexString(curAddr) << endl;
                        fPass = false;
                        break;
                    }
                    curAddr += hexList[i].data.length();
                }
                qStdOut() << "There were" << cErr << "verification errors" << endl;

                unsigned int verifyTime = stopWatch.elapsed();
                qStdOut() << "\n" << "Verification time:" << verifyTime / 1000 << "s" << verifyTime % 1000 << "ms\n" << endl;
            }
            resp = leavePgm();
            fPass &= (!resp.isNull() && resp[1] == STATUS_CMD_OK);
        }
    } else {
        //Sign on failed
        fPass = false;
    }   
    return fPass;
}

QByteArray DigilentPgm::enterPgm(){
    QByteArray body(12, 0x00);
    body[0] = CMD_ENTER_PROGMODE_ISP;

    if(!sendMessage(body)) {
        return NULL;
    }
    return waitForResponse();
}

QByteArray DigilentPgm::loadAddress(unsigned int address) {
    QByteArray body(5, 0x00);
    body[0] = CMD_LOAD_ADDRESS;

    // STK500 uses word aligned addressing, only 31 bits.
    // PIC32 doe not set the MSB in the address as physical addressing is in the HEX file and
    // and has max physical address of 0x1FFFFFFF; all upper bits are virtual address bits.
    address >>= 1;

    body[1] = 0x00;
    body[2] = 0x00;
    body[3] = (((address & 0x0000FF00) >> 8) & 0x000000FF);
    body[4] = (address & 0x000000FF);

    if(!sendMessage(body)) {
        return NULL;
    }
    return waitForResponse();
}

QByteArray DigilentPgm::pgmData(QByteArray data) {
    QByteArray cmd(10, 0x00);
    QByteArray body(data.length() + cmd.length(), 0x00);

    cmd[0] = CMD_PROGRAM_FLASH_ISP;
    cmd[1] = (((data.length() & 0x0000FF00) >> 8) & 0x000000FF);
    cmd[2] = (data.length() & 0x000000FF);

    body.replace(0, cmd.length(), cmd);
    body.replace(cmd.length(), data.length(), data);

    qint32 x = this->port.getBaudRate();
    int y = body.length();
    qStdOut() << endl;


    if(!sendMessage(body)) {
        return NULL;
    }
    return waitForResponse();
}

QByteArray DigilentPgm::readData(unsigned int cbRead) {
    QByteArray body(4, 0x00);
    body[0] = CMD_READ_FLASH_ISP;
    body[1] = (((cbRead & 0x0000FF00) >> 8) & 0x000000FF);
    body[2] = (cbRead & 0x000000FF);

    if(!sendMessage(body)) {
        return NULL;
    }
    return waitForResponse();
}

QByteArray DigilentPgm::leavePgm() {
    QByteArray body(3, 0x00);
    body[0] = CMD_LEAVE_PROGMODE_ISP;

    if(!sendMessage(body)) {
        return NULL;
    }
    return waitForResponse();
}

QTextStream& DigilentPgm::qStdOut()
{
    static QTextStream ts( stdout );
    return ts;
}

void DigilentPgm::releaseDevice() {
    this->port.close();
}
