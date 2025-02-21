#ifndef DIGILENTPGM_H
#define DIGILENTPGM_H

#include <QDebug>
#include <QList>
#include <QSerialPortInfo>
#include <QString>

#include "pgmBlock.h"
#include "../qtSerial/serial.h"

class DigilentPgm
{
public:
    DigilentPgm();
    bool programByPort(QString hexPath, QString portName);
    bool programByBoardName(QString hexPath, QString boardName);
    bool checkInputParameters();
    void releaseDevice();

    static int progress;

private:    
    bool programActivePort(QString hexPath);
    bool findSerialPort(QString strBoardName);
    QByteArray signOn();
    QByteArray createMsg(QByteArray body);
    QByteArray getParam(char param);
    QString getBoardName();
    bool sendMessage(QByteArray body);
    bool isDigilentBootloader();
    bool getBootloaderInfo();
    QVector<int> getBaudRates();
    QByteArray setBaudRate(int baud);
    QByteArray waitForResponse();
    char xorBuffer(QByteArray buffer);

    QVector<PgmBlock> processHexFile(QString hexFileName, int cbBlkSize, unsigned char fill);
    QVector<PgmBlock> createHexFileBlockList(QString hexFileName);
    QVector<PgmBlock> pageHexFileBlockList(QVector<PgmBlock> hexList, int cbBlock, unsigned char hexFill);

    bool stk500v2Pgm(QVector<PgmBlock> hexList);
    QByteArray enterPgm();
    QByteArray loadAddress(unsigned int address);
    QByteArray pgmData(QByteArray data);
    QByteArray readData(unsigned int cbRead);
    QByteArray leavePgm();

    //Utilities
    QTextStream& qStdOut();
    unsigned short byteArrayToUShort(QByteArray array);
    unsigned int byteArrayToUInt(QByteArray array);
    QString toHexString(unsigned int value);

    //Variables
    static QString argHexFile;
    static QString argComPort;
    static QString argBoardName;

    QList<QSerialPortInfo> serialPortInfo;
    Serial port;

    static const char PARAM_BUILD_NUMBER_LOW            = 0x80;
    static const char PARAM_BUILD_NUMBER_HIGH           = 0x81;
    static const char PARAM_HW_VER                      = 0x90;
    static const char PARAM_SW_MAJOR                    = 0x91;
    static const char PARAM_SW_MINOR                    = 0x92;
    static const char PARAM_SW_MAJOR_SLAVE1             = 0xA8;
    static const char PARAM_SW_MINOR_SLAVE1             = 0xA9;
    static const char PARAM_SW_MAJOR_SLAVE2             = 0xAA;
    static const char PARAM_SW_MINOR_SLAVE2             = 0xAB;

    //Digilent specific parameters
    static const char  PARAM_DIGILENT_BLK_SIZE_LOW      = 0xD0;
    static const char  PARAM_DIGILENT_BLK_SIZE_HIGH     = 0xD1;

    static const char VEND_DIGILENT                     = 0x01;
    static const int timeOutSTK500Read                  = 5000; // 5s, no less than 3.5s

    //stk500v2 constants
    static const char CMD_SIGN_ON                       = 0x01;
    static const char CMD_SET_PARAMETER                 = 0x02;
    static const char CMD_GET_PARAMETER                 = 0x03;
    static const char CMD_LOAD_ADDRESS                  = 0x06;
    static const char CMD_ENTER_PROGMODE_ISP            = 0x10;
    static const char CMD_LEAVE_PROGMODE_ISP            = 0x11;
    static const char CMD_PROGRAM_FLASH_ISP             = 0x13;
    static const char CMD_READ_FLASH_ISP                = 0x14;
    static const char CMD_SET_BAUD_RATE                 = 0xD0;
    static const char CMD_GET_BAUD_RATES                = 0xD1;
    static const char CMD_GET_BOOTLOADER_INFO           = 0xD2;
    static const char CMD_GET_BOARD_NAME                = 0xD3;

    static const char STATUS_CMD_OK                     = 0x00;
    static const int SIGNATURE_BYTES                    = 0x504943;
    static const int cbSTK500Blk                        = 256;

    static const int cbMsgOverhead                      = 6;
    static const int iMsgBodyMSB                        = 2;
    static const int iMsgBodyLSB                        = 3;
    static const int iMsgBody                           = 5;

    static bool fDigilentBootloader;
    static char seqNbrSend;
    static char seqNbrRec;
    static int  pgmBlkSize;
    static int  uartBaud;

    static int vendorID;
    static int prodID;
    static unsigned int devID;
    static unsigned int bootloaderVersion;
    static int capabilities;
    static int flashRowSize;
    static int flashPageSize;
};

#endif // DIGILENTPGM_H
