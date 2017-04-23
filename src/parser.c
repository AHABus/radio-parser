#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "RTXDecoder.h"
#include "logger.h"

#define HEXDUMP_COLS        16
#define PRINT_STDOUT        1

uint8_t packet_data[2048];
uint16_t packet_offset = 0;

void printData(FILE* f, uint8_t* data, uint16_t length) {
    if(!f) { return; }
    uint16_t lines = length / HEXDUMP_COLS;
    
    for(uint16_t i = 0; i <= lines; ++i) {
        uint8_t* line = data + (i * HEXDUMP_COLS);
        uint16_t columns = length - (i * HEXDUMP_COLS);
        if(columns > HEXDUMP_COLS) { columns = HEXDUMP_COLS; }
        if(columns <= 0) { break; }
        
        fprintf(f, "%04x: ", (i * HEXDUMP_COLS));
        for(uint8_t j = 0; j < columns; ++j) {
            fprintf(f, "%02x ", line[j]);
        }
        
        for(uint8_t j = columns; j < HEXDUMP_COLS; ++j) {
            fprintf(f, "   ");
        }
        
        fprintf(f, "       ");
        for(uint8_t j = 0; j < columns; ++j) {
            char c = (isprint(line[j])) ? line[j] : '.';
            fprintf(f, "%c", c);
        }
        fprintf(f, "\n");
    }
    
    fprintf(f, "-------\n> ");
    for(uint16_t i = 0; i < length; ++i) {
        if(data[i] == '\n') {
            fprintf(f, "\n> ");
        }
        if(!isprint(data[i])) { continue; }
        fprintf(f, "%c", data[i]);
    }
    fprintf(f, "\n");
}

void dumpPayloadData(FILE* f, uint8_t ID, uint8_t* data, uint16_t length) {
    if(!f) { return; }
    fprintf(f, "PKT/%ld", time(NULL));
    fwrite(data, sizeof(uint8_t), length, f);
}

void printPacket(FILE* f, RTXPacketHeader* header, uint8_t* data) {
    if(!f) { return; }
    double lat = (double)header->latitude / 10000.0;
    double lon = (double)header->longitude / 10000.0;
    
    fprintf(f, "\n///// FCORE//PACKET START /////\n");
    fprintf(f, "rx time:   %ld\n", time(NULL));
    fprintf(f, "latitude:  %f°\n", lat);
    fprintf(f, "longitude: %f°\n", lon);
    fprintf(f, "altitude:  %dm\n", header->altitude);
    fprintf(f, "payload:   %d\n", header->payloadID);
    fprintf(f, "length:    %d bytes\n", header->length);
    fprintf(f, "======\n");
    printData(f, packet_data, header->length);
    fprintf(f, "\n////// FCORE//PACKET END //////\n");
}

void printLocation(RTXPacketHeader* header) {
    
    
    double lat = (double)header->latitude / 10000.0;
    double lon = (double)header->longitude / 10000.0;
    PRINT_DATA("\t>>>> FIX(%f°, %f°, %hum)\n", lat, lon, header->altitude);
    
    FILE* f = fopen("loc.csv", "a");
    if(!f) { return; }
    fprintf(f, "%ld, %f, %f, %hu", time(NULL), lat, lon, header->altitude);
    fclose(f);
}

void dumpPayloadPacket(RTXPacketHeader* header, bool valid) {
    uint8_t ID = header->payloadID;
    
    static char txtFileName[256];
    snprintf(txtFileName, 256, "payload-0x%02x-%s.log", ID, valid ? "good" : "bad");
    static char binFileName[256];
    snprintf(binFileName, 256, "payload-0x%02x.bin", ID);
    
    FILE* txt = fopen(txtFileName, "a");
    if(txt) {
        printPacket(txt, header, packet_data);
#ifdef PRINT_STDOUT
        printPacket(stdout, header, packet_data);
#endif
        fclose(txt);
    }
    
    FILE* bin;
    if(valid && (bin = fopen(binFileName, "ab"))) {
        dumpPayloadData(bin, ID, packet_data, header->length);
        fclose(bin);
    }
}

void dumpSystemPacket(RTXPacketHeader* header, bool valid) {
    packet_data[header->length] = '\0';
    PRINT_DATA("\t>>>> %s\n", packet_data);
}

void packetReceived(RTXPacketHeader* header, bool valid) {
    
    if(header->payloadID >= 10) {
        PRINT_LOG("decoded %s payload packet (0x%02x)\n", valid ? "valid" : "invalid", header->payloadID);
        printLocation(header);
        dumpPayloadPacket(header, valid);
    }
    else if(header->payloadID != 0x00) {
        header->payloadID = 0xff;
        PRINT_LOG("decoded %s unknown packet\n", valid ? "valid" : "invalid");
        printLocation(header);
        dumpPayloadPacket(header, valid);
    } else {
        //we have a system message
        PRINT_LOG("decoded %s system packet\n", valid ? "valid" : "invalid");
        printLocation(header);
        dumpSystemPacket(header, valid);
    }
    packet_offset = 0;
}

bool packetWrite(uint8_t byte, void* userData) {
    if(packet_offset >= 2048) { return false; }
    packet_data[packet_offset++] = byte;
    return true;
}

int packetRead(uint8_t* byte, void* userData) {
    int fd = *(int*)userData;
    if(recv(fd, byte , 1 , 0) > 0) {
        return 1;
    }
    if(errno == EAGAIN || errno == EWOULDBLOCK) {
        return -1;
    }
    else {
        return 0;
    }
}

int main(int argc, const char** args) {
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        perror("error opening socket");
        return - 1;
    }
    
    struct sockaddr_in server;
    
    struct timeval timeout;
    
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(5555);
    
    if(connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        perror("connect failed");
        close(sock);
        return -1;
    }
    
    fprintf(stderr, "///// FCORE//PACKET PARSER /////\n");
    PRINT_LOG("connected to localhost:5555\n");
    
    RTXCoder decoder;
    
    decoder.sequenceNumber = 0xffff;
    decoder.writeCallback = &packetWrite;
    decoder.readCallback = &packetRead;
    decoder.readData = &sock;
    
    fcore_rtxDecodeFrameStream(&decoder, &packetReceived);
    
    close(sock);
    
    return 0;
}