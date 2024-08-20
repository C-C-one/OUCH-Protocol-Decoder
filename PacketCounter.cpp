// PacketCounter.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <fstream>
#include <string>
#include <numbers>
#include <map>


const int c_PacketInfoLength = 6, c_BlockInfoLength = 2, c_totalFullProtocolOffset = c_PacketInfoLength + c_BlockInfoLength, executedShareByteShift = 32;

const char acceptedType = 'A', systemType = 'S', replacedType = 'U', cancelType = 'C', executedType = 'E';

std::map<uint16_t, int> typeMap = {
    {acceptedType, 0}, //accepted
    {systemType, 1}, //system event
    {replacedType, 2}, //replaced
    {cancelType, 3}, //cancel
    {executedType, 4}, //executed
};

struct OUCHStreamData
{
    uint32_t messageCount[5];
    uint32_t totalExecutedShares;
    uint32_t resumeAmount;
};

struct ChunkCarryoverData 
{
    int carryOverBytes = 0, executedSharesIndex = -1, executedShareIdentifier = 0;
    bool abort = false;
};

void PrintOUCHSummary(std::map<uint16_t, OUCHStreamData> mappedData)
{
    for (auto item = mappedData.begin(); item != mappedData.end(); ++item) {
        auto messageCount = item->second.messageCount;
        std::cout << "Stream " << item->first << std::endl;
        std::cout << " Accepted: " << messageCount[0] << " messages\n";
        std::cout << " System Event: " << messageCount[1] << " messages\n";
        std::cout << " Replaced: " << messageCount[2] << " messages\n";
        std::cout << " Canceled: " << messageCount[3] << " messages\n";
        std::cout << " Executed: " << messageCount[4] << " messages: " << item->second.totalExecutedShares << " executed shares \n\n";
    }
}

bool IsMessageLengthNormal(uint32_t length) 
{
    return length == 29 || length == 41 || length == 80 || length == 66 || length == 11;
}

void PrintHeader() 
{
    std::cout << "============================================\n";
    std::cout << "|                  C++                     |\n";
    std::cout << "|           C-C-One 2024-04-20             |\n";
    std::cout << "============================================\n";
}

ChunkCarryoverData ProcessFile(unsigned char* chunk, std::map<uint16_t, OUCHStreamData>* ouchMap, int chunkSize, ChunkCarryoverData carryOver, bool checkError)
{
    int currentIndex = carryOver.carryOverBytes;
    int carryoverExecuteIndex = carryOver.executedSharesIndex;
    if (carryoverExecuteIndex != -1)
    {
        uint32_t ExecutedShares = (chunk[carryoverExecuteIndex] << 24) | (chunk[carryoverExecuteIndex + 1] << 16) | (chunk[carryoverExecuteIndex + 2] << 8) | (chunk[carryoverExecuteIndex + 3]);
        (*ouchMap)[carryOver.executedShareIdentifier].totalExecutedShares += ExecutedShares;
    }
    ChunkCarryoverData carryData = ChunkCarryoverData();
    while (currentIndex < chunkSize)
    {
        uint16_t streamIdentifier = (chunk[currentIndex] << 8) | (chunk[currentIndex + 1]);
        uint32_t packetLength = (chunk[currentIndex + 2] << 24) | (chunk[currentIndex + 3]) << 16 | (chunk[currentIndex + 4]) << 8 | (chunk[currentIndex + 5]);

        uint16_t OUCHMessageLength = (chunk[currentIndex + 6] << 8) | (chunk[currentIndex + 7]);

        char messageType = chunk[currentIndex + 9];

        if (ouchMap->count(streamIdentifier))
        {
            OUCHStreamData streamData = (*ouchMap)[streamIdentifier];
            if (streamData.resumeAmount != 0)
            {
                currentIndex += streamData.resumeAmount + c_totalFullProtocolOffset;
                streamData.resumeAmount = 0;
                (*ouchMap)[streamIdentifier] = streamData;
                continue;
            }
        }
        else
        {
            (*ouchMap).insert({ streamIdentifier, OUCHStreamData() });
        }

        if (checkError && !IsMessageLengthNormal(OUCHMessageLength)) 
        {
            carryData.abort = true;
            std::cerr << "!!!!!!!!!!Message with abnormal size found: with length of " << OUCHMessageLength << ", aborting...\n\n";
            return carryData;
        }

        if (packetLength < OUCHMessageLength + c_BlockInfoLength)
        {
            currentIndex += packetLength + c_PacketInfoLength;
            (*ouchMap)[streamIdentifier].resumeAmount = OUCHMessageLength - packetLength;
            continue;
        }

        (*ouchMap)[streamIdentifier].messageCount[typeMap[messageType]]++;
        if (messageType == executedType) 
        {
            int targetedIndex = currentIndex + executedShareByteShift;
            if (targetedIndex >= chunkSize) 
            {
                carryData.executedSharesIndex = targetedIndex - chunkSize;
                carryData.executedShareIdentifier = streamIdentifier;
            }
            else 
            {
                uint32_t ExecutedShares = (chunk[targetedIndex] << 24) | (chunk[targetedIndex + 1] << 16) | (chunk[targetedIndex + 2] << 8) | (chunk[targetedIndex + 3]);
                (*ouchMap)[streamIdentifier].totalExecutedShares += ExecutedShares;
            }
        }

        currentIndex += OUCHMessageLength + c_totalFullProtocolOffset;
    }
    carryData.carryOverBytes = currentIndex - chunkSize;
    return carryData;
}

int main()
{
    PrintHeader();
    bool checkError;
    while (true)
    {
        std::string response;
        std::cout << "Would you like to pause this program when uncertainty described in the email has been detected? (Y/N)\n";
        std::getline(std::cin, response);
        if (response == "Y") 
        {
            checkError = true;
            break;
        }
        else if (response == "N") 
        {
            checkError = false;
            break;
        }
    }

    while (true) 
    {
        std::string path;
        std::cout << "Please enter the absolute path of the .packets file, or \"QUIT\" to quit...\n";
        std::getline(std::cin, path);

        if (path == "QUIT") 
        {
            return 0;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file) 
        {
            std::cerr << "Failed to open file. Dublecheck the address and enter again...\n";
            continue;
        }

        std::map<uint16_t, OUCHStreamData> ouchMap;

        unsigned char chunk[2048];
        ChunkCarryoverData carryData = ChunkCarryoverData();
        while (file.read(reinterpret_cast<char*>(chunk), 2048))
        {
            carryData = ProcessFile(chunk, &ouchMap, 2048, carryData, checkError);
            if (carryData.abort)
            {
                break;
            }
        }

        if (carryData.abort)
        {
            PrintOUCHSummary(ouchMap);
            std::cerr << "!!!!!!!!!!An error has occured, please refer back to the output\n\n";
        }
        else 
        {
            auto remainingBytes = file.gcount();
            if (remainingBytes > 0) {
                carryData = ProcessFile(chunk, &ouchMap, remainingBytes, carryData, checkError);
            }
            PrintOUCHSummary(ouchMap);
        }
        file.close();
    }
    //delete[] readbytes;
    return 0;
}
