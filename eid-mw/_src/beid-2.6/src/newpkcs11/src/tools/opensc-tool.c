/*
 * opensc-tool.c: Tool for accessing SmartCards with libopensc
 *
 * Copyright (C) 2001  Juha Yrj�l� <juha.yrjola@iki.fi>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef __APPLE__
#include <qapplication.h>
#include <CoreFoundation/CFBundle.h> 
#endif


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <opensc/opensc.h>
#include "util.h"

const char *app_name = "beid-tool";

int opt_reader = -1, opt_no_cache = 0, opt_debug = 0, opt_wait = 0;
char * opt_apdus[8];
int opt_apdu_count = 0;
int quiet = 0;

const struct option options[] = {
	{ "atr",		0, 0,		'a' },
	{ "name",		0, 0,		'n' },
	{ "list-readers",	0, 0, 		'l' },
	{ "reader",		1, 0,		'r' },
	{ "wait",		0, 0,		'w' },
	{ 0, 0, 0, 0 }
};

const char *option_help[] = {
	"Prints the ATR bytes of the card",
	"Identify the card and print its name",
	"Lists all configured readers",
	"Use reader number <arg> [0]",
	"Wait for a card to be inserted",
};

struct sc_context *ctx = NULL;
struct sc_card *card = NULL;

int list_readers(void)
{
	int i;

	if (ctx->reader_count == 0) {
		printf("No readers configured!\n");
		return 0;
	}
	printf("Readers known about:\n");
	printf("Nr.    Driver     Name\n");
	for (i = 0; i < ctx->reader_count; i++) {
		printf("%-7d%-11s%s\n", i, ctx->reader[i]->driver->short_name,
		       ctx->reader[i]->name);
	}
	return 0;
}

int list_reader_drivers(void)
{
	int i;

	if (ctx->reader_drivers[0] == NULL) {
		printf("No reader drivers installed!\n");
		return 0;
	}
	printf("Configured reader drivers:\n");
	for (i = 0; ctx->reader_drivers[i] != NULL; i++) {
		printf("  %-16s %s\n", ctx->reader_drivers[i]->short_name,
		       ctx->reader_drivers[i]->name);
	}
	return 0;
}

int list_drivers(void)
{
	int i;

	if (ctx->card_drivers[0] == NULL) {
		printf("No card drivers installed!\n");
		return 0;
	}
	printf("Configured card drivers:\n");
	for (i = 0; ctx->card_drivers[i] != NULL; i++) {
		printf("  %-16s %s\n", ctx->card_drivers[i]->short_name,
		       ctx->card_drivers[i]->name);
	}
	return 0;
}

int print_file(struct sc_card *card, const struct sc_file *file, const struct sc_path *path, int depth)
{
	int r;
	const char *tmps;
	const char *ac_ops_df[] = {
		"select", "lock", "delete", "create", "rehab", "inval",
		"list"
	};
	const char *ac_ops_ef[] = {
		"read", "update", "write", "erase", "rehab", "inval"
	};

	for (r = 0; r < depth; r++)
		printf("  ");
	for (r = 0; r < path->len; r++) {
		printf("%02X", path->value[r]);
		if (r && (r & 1) == 1)
			printf(" ");
	}
	if (file->namelen) {
		printf("[");
		print_binary(stdout, file->name, file->namelen);
		printf("] ");
	}
	switch (file->type) {
	case SC_FILE_TYPE_WORKING_EF:
		tmps = "wEF";
		break;
	case SC_FILE_TYPE_INTERNAL_EF:
		tmps = "iEF";
		break;
	case SC_FILE_TYPE_DF:
		tmps = " DF";
		break;
	default:
		tmps = "unknown";
		break;
	}
	printf("type: %-3s, ", tmps);
	if (file->type != SC_FILE_TYPE_DF) {
		const char *structs[] = {
			"unknown", "transpnt", "linrfix", "linrfix(TLV)",
			"linvar", "linvar(TLV)", "lincyc", "lincyc(TLV)"
		};
		printf("ef structure: %s, ", structs[file->ef_structure]);
	}
	printf("size: %d\n", file->size);
	for (r = 0; r < depth; r++)
		printf("  ");
	if (file->type == SC_FILE_TYPE_DF)
		for (r = 0; r < sizeof(ac_ops_df)/sizeof(ac_ops_df[0]); r++)
			printf("%s[%s] ", ac_ops_df[r], acl_to_str(sc_file_get_acl_entry(file, r)));
	else
		for (r = 0; r < sizeof(ac_ops_ef)/sizeof(ac_ops_ef[0]); r++)
			printf("%s[%s] ", ac_ops_ef[r], acl_to_str(sc_file_get_acl_entry(file, r)));

	if (file->sec_attr_len) {
		printf("sec: ");
		/* Octets are as follows:
		 *   DF: select, lock, delete, create, rehab, inval
		 *   EF: read, update, write, erase, rehab, inval
		 * 4 MSB's of the octet mean:
		 *  0 = ALW, 1 = PIN1, 2 = PIN2, 4 = SYS,
		 * 15 = NEV */
		hex_dump(stdout, file->sec_attr, file->sec_attr_len, ":");
	}
	if (file->prop_attr_len) {
		printf("\n");
		for (r = 0; r < depth; r++)
			printf("  ");
		printf("prop: ");
		hex_dump(stdout, file->prop_attr, file->prop_attr_len, ":");
	}
	printf("\n\n");

	if (file->type == SC_FILE_TYPE_DF)
		return 0;

	if (file->ef_structure == SC_FILE_EF_TRANSPARENT) {
		unsigned char *buf;
		
		if (!(buf = (unsigned char *) malloc(file->size))) {
			fprintf(stderr, "out of memory");
			return 1;
		}

		r = sc_read_binary(card, 0, buf, file->size, 0);
		if (r > 0)
			hex_dump_asc(stdout, buf, r, 0);
		free(buf);
	} else {
		unsigned char buf[256];
		int i;

		for (i=0; i < file->record_count; i++) {
			printf("Record %d\n", i);
			r = sc_read_record(card, i, buf, 256, 0);
			if (r > 0)
				hex_dump_asc(stdout, buf, r, 0);
		}
	}
	return 0;
}

int enum_dir(struct sc_path path, int depth)
{
	struct sc_file *file;
	int r, file_type;
	u8 files[SC_MAX_APDU_BUFFER_SIZE];

	r = sc_select_file(card, &path, &file);
	if (r) {
		fprintf(stderr, "SELECT FILE failed: %s\n", sc_strerror(r));
		return 1;
	}
	print_file(card, file, &path, depth);
	file_type = file->type;
	sc_file_free(file);
	if (file_type == SC_FILE_TYPE_DF) {
		int i;

		r = sc_list_files(card, files, sizeof(files));
		if (r <= 0) {
			fprintf(stderr, "sc_list_files() failed: %s\n", sc_strerror(r));
			return 1;
		}
		for (i = 0; i < r/2; i++) {
			struct sc_path tmppath;

			memcpy(&tmppath, &path, sizeof(path));
			memcpy(tmppath.value + tmppath.len, files + 2*i, 2);
			tmppath.len += 2;
			enum_dir(tmppath, depth + 1);
		}
	}
	return 0;
}	

int list_files(void)
{
	struct sc_path path;
	int r;
	
	sc_format_path("3F00", &path);
	r = enum_dir(path, 0);
	return r;
}

int send_apdu(void)
{
	struct sc_apdu apdu;
	u8 buf[SC_MAX_APDU_BUFFER_SIZE], sbuf[SC_MAX_APDU_BUFFER_SIZE],
	   rbuf[SC_MAX_APDU_BUFFER_SIZE], *p;
	size_t len, len0, r;
	int c;

	for (c = 0; c < opt_apdu_count; c++) {
		len0 = sizeof(buf);
		sc_hex_to_bin(opt_apdus[c], buf, &len0);
		if (len0 < 4) {
			fprintf(stderr, "APDU too short (must be at least 4 bytes).\n");
			return 2;
		}
		len = len0;
		p = buf;
		memset(&apdu, 0, sizeof(apdu));
		apdu.cla = *p++;
		apdu.ins = *p++;
		apdu.p1 = *p++;
		apdu.p2 = *p++;
		apdu.resp = rbuf;
		apdu.resplen = sizeof(rbuf);
		len -= 4;
		if (len > 1) {
			apdu.lc = *p++;
			len--;
			memcpy(sbuf, p, apdu.lc);
			apdu.data = sbuf;
			apdu.datalen = apdu.lc;
			if (len < apdu.lc) {
				fprintf(stderr, "APDU too short (need %d bytes).\n",
					apdu.lc-len);
				return 2;
			}
			len -= apdu.lc;
			if (len) {
				apdu.le = *p++;
				if (apdu.le == 0)
					apdu.le = 256;
				len--;
				apdu.cse = SC_APDU_CASE_4_SHORT;
			} else
				apdu.cse = SC_APDU_CASE_3_SHORT;
			if (len) {
				fprintf(stderr, "APDU too long (%d bytes extra).\n", len);
				return 2;
			}
		} else if (len == 1) {
			apdu.le = *p++;
			if (apdu.le == 0)
				apdu.le = 256;
			len--;
			apdu.cse = SC_APDU_CASE_2_SHORT;
		} else
			apdu.cse = SC_APDU_CASE_1;
		printf("Sending: ");
		for (r = 0; r < len0; r++)
			printf("%02X ", buf[r]);
		printf("\n");
	#if 0
		ctx->debug = 5;
	#endif
		r = sc_transmit_apdu(card, &apdu);
	#if 0
		ctx->debug = opt_debug;
	#endif
		if (r) {
			fprintf(stderr, "APDU transmit failed: %s\n", sc_strerror(r));
			return 1;
		}
		printf("Received (SW1=0x%02X, SW2=0x%02X)%s\n", apdu.sw1, apdu.sw2,
		       apdu.resplen ? ":" : "");
		if (apdu.resplen)
			hex_dump_asc(stdout, apdu.resp, apdu.resplen, -1);
	}
	return 0;
}

int main(int argc, char * *argv)
{
	int err = 0, r, c, long_optind = 0;
	int do_list_readers = 0;
	int do_list_drivers = 0;
	int do_list_rdrivers = 0;
	int do_list_files = 0;
	int do_send_apdu = 0;
	int do_print_atr = 0;
	int do_print_name = 0;
	int action_count = 0;
	const char *opt_driver = NULL;

#ifdef __APPLE__
        static char app_path[300];

        CFURLRef pluginRef = CFBundleCopyBundleURL(CFBundleGetMainBundle());
        CFStringRef macPath = CFURLCopyFileSystemPath(pluginRef, 
                                               kCFURLPOSIXPathStyle);
        const char *pathPtr = CFStringGetCStringPtr(macPath, 
                                               CFStringGetSystemEncoding());
	strcpy(app_path, pathPtr);
        CFRelease(pluginRef);
        CFRelease(macPath);

        int i = 1;
        static char *s = app_path; // "/usr/local/lib/libbelpicgui.dylib";
        QApplication app(i, &s);
#endif

		
	while (1) {
		c = getopt_long(argc, argv, "nlfr:qds:DRc:aw", options, &long_optind);
		if (c == -1)
			break;
		if (c == '?')
			print_usage_and_die();
		switch (c) {
		case 'l':
			do_list_readers = 1;
			action_count++;
			break;
		case 'a':
			do_print_atr = 1;
			action_count++;
			break;
		case 'n':
			do_print_name = 1;
			action_count++;
			break;
		case 'r':
			opt_reader = atoi(optarg);
			break;
		case 'w':
			opt_wait = 1;
			break;
		}
	}
	if (action_count == 0)
		print_usage_and_die();
	r = sc_establish_context(&ctx, app_name);
	if (r) {
		fprintf(stderr, "Failed to establish context: %s\n", sc_strerror(r));
		return 1;
	}
	if (opt_debug)
		ctx->debug = opt_debug;
	if (do_list_rdrivers) {
		if ((err = list_reader_drivers()))
			goto end;
		action_count--;
	}
	if (do_list_readers) {
		if ((err = list_readers()))
			goto end;
		action_count--;
	}
	if (do_list_drivers) {
		if ((err = list_drivers()))
			goto end;
		action_count--;
	}
	if (action_count <= 0)
		goto end;

	if (opt_driver != NULL) {
		err = sc_set_card_driver(ctx, opt_driver);
		if (err) {
			fprintf(stderr, "Driver '%s' not found!\n", opt_driver);
			err = 1;
			goto end;
		}
	}

	err = connect_card(ctx, &card, opt_reader, 0, opt_wait, quiet);
	if (err)
		goto end;

	if (!quiet)
		printf("Using card driver: %s\n", card->driver->name);
	r = sc_lock(card);
	if (r) {
		fprintf(stderr, "Unable to lock card: %s\n", sc_strerror(r));
		err = 1;
		goto end;
	}
	if (do_print_atr) {
		if (!quiet)
			printf("Card ATR: ");
		hex_dump_asc(stdout, card->atr, card->atr_len, -1);
		action_count--;
	}
	if (do_print_name) {
		if (!quiet)
			printf("Card name: ");
		printf("%s\n", card->name);
		action_count--;
	}
	if (do_send_apdu) {
		if ((err = send_apdu()))
			goto end;
		action_count--;
	}
	
	if (do_list_files) {
		if ((err = list_files()))
			goto end;
		action_count--;
	}
end:
	if (card) {
		sc_unlock(card);
		sc_disconnect_card(card, 0);
	}
	if (ctx)
		sc_release_context(ctx);
	return err;
}