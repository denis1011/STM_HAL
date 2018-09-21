/* Copyright (C) 2015  Nils Weiss
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include "AT_Parser.h"
#include "trace.h"
#include <vector>
#include "LockGuard.h"
#include "binascii.h"

using app::AT;
using app::ATCmd;
using app::ATCmdERROR;
using app::ATCmdOK;
using app::ATCmdUPSND;
using app::ATCmdURC;
using app::ATCmdUSORF;
using app::ATCmdUSOST;
using app::ATParser;

static constexpr const int __attribute__((unused)) g_DebugZones = 0; // ZONE_ERROR | ZONE_WARNING | ZONE_VERBOSE | ZONE_INFO;

void AT::okReceived(void)
{
    Trace(ZONE_INFO, "%s OK\n", mName.data());
    while (true) {}
}
void AT::errorReceived(void)
{
    Trace(ZONE_INFO, "%s ERROR\n", mName.data());
    while (true) {}
}
AT::Return_t AT::onResponseMatch(void)
{
    Trace(ZONE_INFO, "Hello from %s\n", mName.data());
    return Return_t::WAITING;
}

AT::Return_t ATCmd::send(AT::SendFunction& sendFunction, const std::chrono::milliseconds timeout)
{
    mCommandSuccess = false;

    os::LockGuard<os::Mutex> lock(mParser.mSendingMutex, std::chrono::milliseconds(10).count());
    if (!lock) {
        Trace(ZONE_INFO, "Parser not ready for sending\n");
        return Return_t::TRY_AGAIN;
    }

    if (mParser.mWaitingCmd) {
        Trace(ZONE_INFO, "Parser not ready\n");
        return Return_t::TRY_AGAIN;
    }
    Trace(ZONE_INFO, "sending: %s\r\n", mRequest.data());
    if (sendFunction(mRequest, timeout) != mRequest.length()) {
        Trace(ZONE_ERROR, "Couldn't send\n");
        return Return_t::ERROR;
    }
    mParser.mWaitingCmd = this->shared_from_this();

    if (mSendDone.take(timeout) && mCommandSuccess) {
        return Return_t::FINISHED;
    }
    return Return_t::ERROR;
}

bool ATCmd::wasExecutionSuccessful(void) const
{
    return mCommandSuccess;
}

void ATCmd::okReceived(void)
{
    Trace(ZONE_INFO, "ATCMD: %s OK\n", mName.data());
    mCommandSuccess = true;
    mSendDone.give();
}
void ATCmd::errorReceived(void)
{
    Trace(ZONE_INFO, "ATCMD: %s ERROR\n", mName.data());
    mCommandSuccess = false;
    mSendDone.give();
}

AT::Return_t ATCmdUSOST::send(std::string_view data, std::chrono::milliseconds timeout)
{
    return this->send(data, mIp, mPort, timeout);
}

AT::Return_t ATCmdUSOST::send(std::string_view          data,
                              std::string_view          ip,
                              std::string_view          port,
                              std::chrono::milliseconds timeout)
{
    return this->send(mSocket, ip, port, data, timeout);
}

AT::Return_t ATCmdUSOST::send(const size_t              socket,
                              std::string_view          ip,
                              std::string_view          port,
                              std::string_view          data,
                              std::chrono::milliseconds timeout)
{
    if (data.length() == 0) {
        Trace(ZONE_VERBOSE, "Nodata %d\r\n", data.length());
        return AT::Return_t::FINISHED;
    }
    mData = data;
    std::string request = "AT+USOST=" +
                          std::to_string(socket) + ",\"" +
                          std::string(ip.data(), ip.length()) + "\"," +
                          std::string(port.data(), port.length()) + "," +
                          std::to_string(data.length()) + "\r";

    mRequest = request;
    mWaitingForPrompt = true;
    auto ret = ATCmd::send(mSendFunction, timeout);
    mWaitingForPrompt = false;
    if ((ret == AT::Return_t::FINISHED) && mCommandSuccess) {
        return AT::Return_t::FINISHED;
    }
    return AT::Return_t::ERROR;
}

AT::Return_t ATCmdUSOST::onResponseMatch(void)
{
    if (!mWaitingForPrompt) {
        return Return_t::ERROR;
    }
    if (mSendFunction(mData, std::chrono::milliseconds(100)) != mData.length()) {
        Trace(ZONE_ERROR, "Couldn't send data\n");
        return Return_t::ERROR;
    }
    return Return_t::WAITING;
}

AT::Return_t ATCmdUSORF::send(size_t bytesToRead, std::chrono::milliseconds timeout)
{
    if (bytesToRead > ATParser::BUFFERSIZE) {
        return Return_t::ERROR;
    }
    Trace(ZONE_INFO, "SEND USORF\r\n");
    mRequest = std::string("AT+USORF=" +
                           std::to_string(mSocket) + "," +
                           std::to_string(bytesToRead) + "\r");
    return ATCmd::send(mSendFunction, timeout);
}

AT::Return_t ATCmdUSORF::onResponseMatch(void)
{
    auto socketstring = mParser.getInputUntilComma();
    if (socketstring.length() == 0) {
        return AT::Return_t::ERROR;
    }
    mSocket = std::stoul(std::string(socketstring.data(), socketstring.length()));

    char termination = 0;
    auto ipstring = mParser.getInputUntilComma(std::chrono::milliseconds(500), &termination);

    if (termination == ',') {
        if (ipstring.length() == 0) {
            return AT::Return_t::ERROR;
        }
        mIp = std::string(ipstring.data(), ipstring.length());

        auto portstring = mParser.getInputUntilComma();
        if (portstring.length() == 0) {
            return AT::Return_t::ERROR;
        }
        mPort = std::string(portstring.data(), portstring.length());

        auto bytesAvailablestring = mParser.getInputUntilComma();
        if (bytesAvailablestring.length() == 0) {
            return AT::Return_t::ERROR;
        }
        auto bytesAvailable = std::stoul(std::string(bytesAvailablestring.data(), bytesAvailablestring.length()));

        auto datastring = mParser.getBytesFromInput(bytesAvailable + 2);
        if (datastring.length() < bytesAvailable) {
            return Return_t::ERROR;
        }

        auto first = datastring.find_first_of('"') + 1;
        auto last = datastring.find_last_of('"');

        mData = std::string(datastring.data() + first, last - first);

        return Return_t::WAITING;
    } else if (termination == '\r') {
        auto bytesAvailablestring = ipstring;
        auto bytesAvailable = std::stoul(std::string(bytesAvailablestring.data(), bytesAvailablestring.length()));
        if (bytesAvailable) {
            mUrcReceivedCallback(mSocket, bytesAvailable);
        }
        return Return_t::FINISHED;
    } else {
        return Return_t::ERROR;
    }
}

AT::Return_t ATCmdUPSND::send(const size_t socket, const size_t parameter, const std::chrono::milliseconds timeout)
{
    Trace(ZONE_INFO, "SEND UPSND\r\n");
    mRequest = std::string("AT+UPSND=" +
                           std::to_string(socket) + "," +
                           std::to_string(parameter) + "\r");
    return ATCmd::send(mSendFunction, timeout);
}

AT::Return_t ATCmdUPSND::onResponseMatch(void)
{
    auto socketstring = mParser.getInputUntilComma();
    if (socketstring.length() == 0) {
        return AT::Return_t::ERROR;
    }
    mSocket = std::stoul(std::string(socketstring.data(), socketstring.length()));

    auto parameterstring = mParser.getInputUntilComma();
    if (parameterstring.length() == 0) {
        return AT::Return_t::ERROR;
    }
    mParameter = std::stoul(std::string(parameterstring.data(), parameterstring.length()));

    auto datastring = mParser.getInputUntilComma();
    if (datastring.length() == 0) {
        return AT::Return_t::ERROR;
    }

    auto first = datastring.find_first_of('"') + 1;
    auto last = datastring.find_last_of('"');

    mData = std::string(datastring.data() + first, last - first);

    return Return_t::FINISHED;
}

AT::Return_t ATCmdURC::onResponseMatch(void)
{
    char termination = 0;
    auto socketstring = mParser.getInputUntilComma(std::chrono::milliseconds(50), &termination);
    size_t socket = std::stoul(std::string(socketstring.data(), socketstring.length()));

    if (termination == '\r') {
        mUrcReceivedCallback(socket, 0);
        return Return_t::FINISHED;
    }

    std::string_view line = mParser.getLineFromInput();

    size_t bytesAvailable = std::stoul(std::string(line.data(), line.length()));

    if (bytesAvailable > ATParser::BUFFERSIZE) {
        return Return_t::ERROR;
    }

    mUrcReceivedCallback(socket, bytesAvailable);
    return Return_t::FINISHED;
}

AT::Return_t ATCmdOK::onResponseMatch(void)
{
    if (mParser.mWaitingCmd) {
        mParser.mWaitingCmd->okReceived();
        mParser.mWaitingCmd.reset();
        return Return_t::FINISHED;
    }
    return Return_t::ERROR;
}

AT::Return_t ATCmdERROR::onResponseMatch(void)
{
    if (mParser.mWaitingCmd) {
        mParser.mWaitingCmd->errorReceived();
        mParser.mWaitingCmd.reset();
        return Return_t::FINISHED;
    }
    return Return_t::ERROR;
}

std::array<char, ATParser::BUFFERSIZE> ATParser::ReceiveBuffer;

void ATParser::reset(void)
{
    if (mWaitingCmd) {
        Trace(ZONE_INFO, "Toogle Error on waiting cmd\r\n");
        mWaitingCmd->errorReceived();
        mWaitingCmd.reset();
        mSendingMutex.give();
    }
}

bool ATParser::parse(std::chrono::milliseconds timeout)
{
    size_t currentPos = 0;
    auto possibleResponses = mRegisteredATCommands;
    reset();
    Trace(ZONE_INFO, "Start Parser\r\n");

    while (true) {
        if (mReceive(reinterpret_cast<uint8_t*>(ReceiveBuffer.data() + currentPos++), 1, timeout) != 1) {
            Trace(ZONE_ERROR, "Parser Timeout\r\n");
            return false;
        }

        std::string_view currentData(ReceiveBuffer.data(), currentPos);

        Trace(ZONE_VERBOSE, "parse: %s\n", std::string(currentData.data(), currentData.length()).c_str());
        // this vector copy operations should be optimized
        decltype(possibleResponses) sievedResponses;

        for (auto atcmd : possibleResponses) {
            if (currentData == atcmd->mResponse.substr(0, currentPos)) {
                sievedResponses.push_back(atcmd);
            }
        }
        possibleResponses = sievedResponses;

        if (possibleResponses.size() > 1) { continue; }

        if (possibleResponses.size() == 1) {
            auto match = possibleResponses[0];
            if (currentPos < match->mResponse.length()) {
                continue;
            }
            Trace(ZONE_INFO, "MATCH: %s\n", match->mName.data());

            switch (match->onResponseMatch()) {
            case AT::Return_t::WAITING:
                mWaitingCmd = match;
                break;

            case AT::Return_t::ERROR:
                Trace(ZONE_ERROR, "Error occured \r\n");
                break;

            case AT::Return_t::TRY_AGAIN:
                break;

            case AT::Return_t::FINISHED:
                Trace(ZONE_ERROR, "ParserFinished %s \r\n", match->mName.data());
                break;
            }
        }
        currentPos = 0;
        possibleResponses = mRegisteredATCommands;
    }

    return true;
}

void ATParser::registerAtCommand(std::shared_ptr<AT> cmd)
{
    mRegisteredATCommands.push_back(cmd);
}

std::string_view ATParser::getLineFromInput(std::chrono::milliseconds timeout) const
{
    uint8_t data;
    size_t currentPos = 0;

    while (true) {
        if (1 != mReceive(reinterpret_cast<uint8_t*>(&data), 1, timeout)) {
            // timeout
            return "";
        }
        ReceiveBuffer[currentPos++] = data;

        const bool terminationFound = (data == '\r') || (data == '\n') || (data == 0);

        if (terminationFound) {
            break;
        }
    }

    return std::string_view(ReceiveBuffer.data(), currentPos);
}

std::string_view ATParser::getInputUntilComma(std::chrono::milliseconds timeout, char* termination) const
{
    uint8_t data;
    size_t currentPos = 0;

    while (true) {
        if (1 != mReceive(reinterpret_cast<uint8_t*>(&data), 1, timeout)) {
            // timeout
            return "";
        }

        const bool terminationFound = (data == ',') || (data == '\r');

        if (terminationFound || isalpha(data)) {
            if (termination != nullptr) {
                *termination = data;
            }
            break;
        }
        ReceiveBuffer[currentPos++] = data;
    }

    return std::string_view(ReceiveBuffer.data(), currentPos);
}

std::string_view ATParser::getBytesFromInput(size_t numberOfBytes, std::chrono::milliseconds timeout) const
{
    uint8_t data;
    size_t currentPos = 0;

    if (numberOfBytes >= BUFFERSIZE) {
        return "";
    }

    while (true) {
        if (1 != mReceive(reinterpret_cast<uint8_t*>(&data), 1, timeout)) {
            // timeout
            return "";
        }
        ReceiveBuffer[currentPos++] = data;

        const bool terminationFound = currentPos >= numberOfBytes;
        if (terminationFound) {
            break;
        }
    }

    return std::string_view(ReceiveBuffer.data(), currentPos);
}
