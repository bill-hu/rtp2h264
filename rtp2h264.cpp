// rtp2h264.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <stdint.h>
#pragma comment (lib,"Ws2_32.lib")
//noths.obj : error LNK2001: unresolved external ymbol _inet_addr@4
#include <winsock.h>
//#include "rtpdataheader.h"
#include "RtpHeaders.h"
using namespace erizo;
static inline uint8_t nal_header_get_type(const uint8_t *h) {
	return (*h) & ((1 << 5) - 1);
}

static inline uint8_t nal_header_get_nri(const uint8_t *h) {
	return ((*h) >> 5) & 0x3;
}

int process(FILE *fpi,FILE * fpo) {

	int bytes = 0, numBytes = 1280*720 * 3;	/* FIXME */
	uint8_t *received_frame = (uint8_t *)malloc(numBytes);
	uint8_t *buffer_rtp = (uint8_t *)malloc(64000),* buffer;
	int len = 0, frameLen = 0;
	int keyFrame = 0;
	int keyframe_ts = 0;
	RtpHeader * pRtpHeader = NULL;
	uint32_t ts = 0;
	bool bquit = false;


	while (1) {
		keyFrame = 0;
		frameLen = 0;
		len = 0;
		while (1) {
			/* RTP payload */
			fread(&len, sizeof(int), 1, fpi);
			int readlen = fread(buffer_rtp, sizeof(char), len, fpi);
			if (readlen != len) {
				printf("Didn't manage to read all the bytes we needed (%d < %d)...\n", bytes, len);
				bquit = true;
				break;
			}

			pRtpHeader =(erizo::RtpHeader *) buffer_rtp;
			buffer = buffer_rtp + pRtpHeader->getHeaderLength();
			if (!ts) {
				ts = pRtpHeader->timestamp;
			}

			len -= pRtpHeader->getHeaderLength();

			/* H.264 depay */
			int jump = 0;

			uint8_t fragment = *buffer & 0x1F;
			uint8_t nal = *(buffer + 1) & 0x1F;
			uint8_t start_bit = *(buffer + 1) & 0x80;
			if (fragment == 28 || fragment == 29)
				printf("Fragment=%d, NAL=%d, Start=%d (len=%d, frameLen=%d)\n", fragment, nal, start_bit, len, frameLen);
			else
				printf("Fragment=%d (len=%d, frameLen=%d)\n", fragment, len, frameLen);
			if (fragment == 5 ||
				((fragment == 28 || fragment == 29) && nal == 5 && start_bit == 128)) {
				//printf("(seq=%"SCNu16", ts=%"SCNu64") Key frame\n", tmp->seq, pRtpHeader->ts);
				keyFrame = 1;
				/* Is this the first keyframe we find? */
				if (keyframe_ts == 0) {
					keyframe_ts = pRtpHeader->timestamp;
					//printf("First keyframe: %"SCNu64"\n", pRtpHeader->ts - list->ts);
				}
			}
			/* Frame manipulation */
			if ((fragment > 0) && (fragment < 24)) {	/* Add a start code */
				uint8_t *temp = received_frame + frameLen;
				memset(temp, 0x00, 1);
				memset(temp + 1, 0x00, 1);
				memset(temp + 2, 0x01, 1);
				frameLen += 3;
			}
			else if (fragment == 24) {	/* STAP-A */
										/* De-aggregate the NALs and write each of them separately */
				buffer++;
				int tot = len - 1;
				uint16_t psize = 0;
				frameLen = 0;
				while (tot > 0) {
					memcpy(&psize, buffer, 2);
					psize = ntohs(psize);
					buffer += 2;
					tot -= 2;
					/* Now we have a single NAL */
					uint8_t *temp = received_frame + frameLen;
					memset(temp, 0x00, 1);
					memset(temp + 1, 0x00, 1);
					memset(temp + 2, 0x01, 1);
					frameLen += 3;
					memcpy(received_frame + frameLen, buffer, psize);
					frameLen += psize;
					/* Go on */
					buffer += psize;
					tot -= psize;
				}
				/* Done, we'll wait for the next video data to write the frame */
				continue;
			}
			else if ((fragment == 28) || (fragment == 29)) {	/* FIXME true fr FU-A, not FU-B */
				uint8_t indicator = *buffer;
				uint8_t header = *(buffer + 1);
				jump = 2;
				len -= 2;
				if (header & 0x80) {
					/* First part of fragmented packet (S bit set) */
					uint8_t *temp = received_frame + frameLen;
					memset(temp, 0x00, 1);
					memset(temp + 1, 0x00, 1);
					memset(temp + 2, 0x01, 1);
					memset(temp + 3, (indicator & 0xE0) | (header & 0x1F), 1);
					frameLen += 4;
				}
				else if (header & 0x40) {
					/* Last part of fragmented packet (E bit set) */
				}
			}
			memcpy(received_frame + frameLen, buffer + jump, len);
			frameLen += len;
			if (len == 0)
				break;
			/* Check if timestamp changes: marker bit is not mandatory, and may be lost as well */
			if (pRtpHeader->timestamp > ts)
				break;
		}
		if (frameLen > 0) {
			/* Save the frame */
			fwrite(received_frame, frameLen, 1, fpo);
			if (pRtpHeader) {
				ts = pRtpHeader->timestamp;
			}
		}
		if (bquit) {
			break;
		}
	}
	free(received_frame);
	free(buffer_rtp);
	return 0;
}

int main(int argc, char * argv[])
{
	FILE * fpi, * fpo;
	char h264file[1024];
	if (argc < 2) {
		printf("%s rtpfile [h264file]", argv[0]);
		return -1;
	}

	fpi = fopen(argv[1], "rb");
	if (!fpi) {
		printf("can't open %s", argv[1]);
		return -2;
	}

	if (argc < 3) {
		strcpy(h264file, argv[1]);
		strcat(h264file, "_.264");
	}
	else {
		strcpy(h264file, argv[2]);
	}

	fpo = fopen(h264file, "wb");
	if (!fpo) {
		printf("can't open %s", h264file);
		return -3;
	}

	process(fpi, fpo);

	fclose(fpi);
	fclose(fpo);

	return 0;
}

