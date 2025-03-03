/* -*- C++ -*-
 * Copyright 2019-2024 LibRaw LLC (info@libraw.org)
 *
 LibRaw uses code from dcraw.c -- Dave Coffin's raw photo decoder,
 dcraw.c is copyright 1997-2018 by Dave Coffin, dcoffin a cybercom o net.
 LibRaw do not use RESTRICTED code from dcraw.c

 LibRaw is free software; you can redistribute it and/or modify
 it under the terms of the one of two licenses as you choose:

1. GNU LESSER GENERAL PUBLIC LICENSE version 2.1
   (See file LICENSE.LGPL provided in LibRaw distribution archive for details).

2. COMMON DEVELOPMENT AND DISTRIBUTION LICENSE (CDDL) Version 1.0
   (See file LICENSE.CDDL provided in LibRaw distribution archive for details).

 */

#include "../../internal/dcraw_defs.h"
#include "../../internal/libraw_checked_buffer.h"


int LibRaw::guess_RAFDataGeneration (uchar *RAFData_start) // returns offset to first valid width/height pair
{

/* RAFDataGeneration codes, values are 4 bytes, little endian

   RAFData gen. 0: no RAFData
     DBP for GX680 / DX-2000
     E550, E900, (F500 / F505?) F550, F600 / F605, F700, F770 / F775, F800, F810, F900
     HS10 HS11, HS20 / HS22, HS30 / HS33 / HS35, HS50
     S1, SL1000, S100, S200 / S205, S20Pro, S2Pro, S3Pro, S5Pro
     S5000, S5100 / S5500, S5200 / S5600, S6000 / S6500, S7000, S9000 / S9500, S9100 / S9600

   RAFData gen. 1, offset to WH pair (offsetWH_inRAFData) = 0:
   - number in bytes 0..1 is less than 10000
   - contains WH pair, recommended image size WH pair, 16 bytes unknown, 2*13 values (for crops, scales?)
     X100, X-Pro1, X-S1, X10, XF1

   RAFData gen. 2, offset to WH pair = 4:
   - bytes 0..1 contain a number greater than 10000; bytes 2..3 contain zero;
     version is in bytes 0..1, possibly big endian
     - contains WH pair, recommended image size WH pair, 16 bytes unknown, 2*13 values
     X-E1

   RAFData gen. 3, offset to WH pair = 4:
   - bytes 0..1 contain zero; bytes 2..3 contain version;
   - contains a table of 3+2*13 values; first 3 values look like WHW
     X-A1, X-A2, X-E2, X-M1
     X-T1, X-T10
     X100S, X100T
     X20, X30, X70, XQ1, XQ2

   RAFData gen. 4, offset to WH pair = 8:
   - same conditions as for RAFData gen. 3, but also adds WRTS in bytes 4..7
   - contains a table of 3+2*13 values; first 3 values look like WHW
   - H in WHW group has a different meaning if the shot is taken in crop 2 mode
     GFX 100, GFX 100S, GFX 100 II
     GFX 50R, GFX 50S, GFX 50S II
     X-E2S, X-E3, X-H1, X-S10, X-H2
     X-T2, X-T3, X-T4, X-T20, X-T30
     X-Pro2, X-Pro3
     X100F, X100V, X100VI

   RAFData gen. set to 4096:
   - RAFData length is exactly 4096
     X-A3, X-A5, X-A7, X-A10, X-A20
     X-T100, X-T200,
     XF10
*/

  int offsetWH_inRAFData=0; /* clang warns about not initialized value */
  ushort b01  = sget2(RAFData_start);   // bytes 0..1
  ushort b23  = sget2(RAFData_start+2); // bytes 2..3
  int is_WRTS = (sget4(RAFData_start + 4) == 0x53545257); // STRW
  if (b01 && !b23 && (b01<10000))
  {
    imFuji.RAFDataGeneration = 1;
    offsetWH_inRAFData = 0;
  }
  else if ((b01>10000) && !b23)
  {
    imFuji.RAFDataGeneration = 2;
    imFuji.RAFDataVersion = b01;
    offsetWH_inRAFData = 4;
  }
  else if (!b01)
  {
    if (!is_WRTS)
    {
      imFuji.RAFDataGeneration = 3;
      offsetWH_inRAFData = 4;
    }
    else
    {
      imFuji.RAFDataGeneration = 4;
      offsetWH_inRAFData = 8;
    }
    imFuji.RAFDataVersion = b23;
  }

// printf (">> ID: 0x%llx, RAFDataVersion: 0x%04x, RAFDataGeneration: %d\n",
// ilm.CamID, imFuji.RAFDataVersion, imFuji.RAFDataGeneration);

  return offsetWH_inRAFData;
}

class fuji_wb_checked_buffer_t : public checked_buffer_t
{
public:
	bool isWB(unsigned offset)
	{
		return sget2(offset) != 0 && sget2(offset + 2) != 0 && sget2(offset + 4) != 0 && sget2(offset + 6) != 0 &&
			sget2(offset + 8) != 0 && sget2(offset + 10) != 0 && sget2(offset) != 0xff && sget2(offset + 2) != 0xff &&
			sget2(offset + 4) != 0xff && sget2(offset + 6) != 0xff && sget2(offset + 8) != 0xff &&
			sget2(offset + 10) != 0xff && sget2(offset) == sget2(offset + 6) && sget2(offset) < sget2(offset + 2) &&
			sget2(offset) < sget2(offset + 4) && sget2(offset) < sget2(offset + 8) && sget2(offset) < sget2(offset + 10)
			;
	}
	fuji_wb_checked_buffer_t(short ord, int size) : checked_buffer_t(ord, size) {}
	void set_order(short o) { _order = o; }
};

struct tag2wb_t
{
	unsigned tag;
	int wb;
} tag2wbtable[] =
{
	{0x2000, LIBRAW_WBI_Auto },
	{0x2100, LIBRAW_WBI_FineWeather },
	{0x2200, LIBRAW_WBI_Shade},
	{0x2300, LIBRAW_WBI_FL_D },
	{0x2301, LIBRAW_WBI_FL_N},
	{0x2302, LIBRAW_WBI_FL_W },
	{0x2310, LIBRAW_WBI_FL_WW},
	{0x2311, LIBRAW_WBI_FL_L},
	{0x2400, LIBRAW_WBI_Tungsten},
	{0x2410, LIBRAW_WBI_Flash}
};
const int tag2wbtable_size = sizeof(tag2wbtable) / sizeof(tag2wbtable[0]);


void LibRaw::parseAdobeRAFMakernote()
{

  unsigned posPrivateMknBuf=0; /* clang warns about not inited value */
  unsigned PrivateMknLength;
  unsigned PrivateOrder;
  unsigned ifd_start, ifd_len;
  unsigned PrivateEntries, PrivateTagID;
  unsigned PrivateTagBytes;
  int FujiShotSelect;
  unsigned wb_section_offset = 0;
  int posWB;
  int c;

#if 0
#define isWB(posWB)                                                            \
  sget2(posWB) != 0 && sget2(posWB + 2) != 0 && sget2(posWB + 4) != 0 &&       \
      sget2(posWB + 6) != 0 && sget2(posWB + 8) != 0 &&                        \
      sget2(posWB + 10) != 0 && sget2(posWB) != 0xff &&                        \
      sget2(posWB + 2) != 0xff && sget2(posWB + 4) != 0xff &&                  \
      sget2(posWB + 6) != 0xff && sget2(posWB + 8) != 0xff &&                  \
      sget2(posWB + 10) != 0xff && sget2(posWB) == sget2(posWB + 6) &&         \
      sget2(posWB) < sget2(posWB + 2) && sget2(posWB) < sget2(posWB + 4) &&    \
      sget2(posWB) < sget2(posWB + 8) && sget2(posWB) < sget2(posWB + 10)
#endif

#define get_average_WB(wb_index)                                               \
  do {                                                                         \
	  FORC4 icWBC[wb_index][GRGB_2_RGBG(c)] =                                      \
		  PrivateMknBuf.sget2(posPrivateMknBuf + (c << 1));                        \
	  if ((PrivateTagBytes == 16) && average_WBData) {                             \
		FORC4 icWBC[wb_index][GRGB_2_RGBG(c)] =                                    \
				 (icWBC[wb_index][GRGB_2_RGBG(c)] +                                \
				  PrivateMknBuf.sget2(posPrivateMknBuf + (c << 1)+8)) /2;          \
	  }                                                                            \
	  if (use_WBcorr_coeffs) {                                                     \
		icWBC[wb_index][0] = int(icWBC[wb_index][0]*wbR_corr);                     \
		icWBC[wb_index][2] = int(icWBC[wb_index][2]*wbB_corr);                     \
	  }                                                                            \
  } while(0)

  ushort use_WBcorr_coeffs = 0;
  double wbR_corr = 1.0;
  double wbB_corr = 1.0;

  if (strstr(model, "S2Pro")
      || strstr(model, "S20Pro")
      || strstr(model, "F700")
      || strstr(model, "S5000")
      || strstr(model, "S7000")
      ) {
    use_WBcorr_coeffs = 1;
    wbR_corr = 10.0 / 17.0 / 0.652941;
    wbB_corr = 2.0 /3.0 / (3.0 / 4.0 + 1.0 / 300.0);
  } else if (strstr(model, "DBP") || strstr(model, "DX-2000")) {
    use_WBcorr_coeffs = 1;
    wbR_corr = 0.7632653061;
    wbB_corr = 0.8591549296;
  }

  FujiShotSelect = LIM(shot_select, 0, 1);
  int average_WBData = 1;

  order = 0x4d4d;
  PrivateMknLength = get4();

  // At least 0x36 bytes because of memcpy(imFuji.RAFVersion, PrivateMknBuf + 0x32, 4);
  if ((PrivateMknLength >= 0x36) && (PrivateMknLength < 10240000)) // 1024b for safety
  {
	fuji_wb_checked_buffer_t PrivateMknBuf(order, PrivateMknLength+1024);
    fread(PrivateMknBuf.data(), PrivateMknLength, 1, ifp);
    memcpy(imFuji.SerialSignature, PrivateMknBuf.data() + 6, 0x0c);
    imFuji.SerialSignature[0x0c] = 0;
    memcpy(imFuji.SensorID, imFuji.SerialSignature + 0x06, 0x04);
    imFuji.SensorID[0x04] = 0;
    c = 11;
    while (isdigit(imFuji.SerialSignature[c]) && (c>0))
      c--;
    ilm.CamID = unique_id = (unsigned long long)atoi(imFuji.SerialSignature+c+1);
    memcpy(model, PrivateMknBuf.data() + 0x12, 0x20);
    model[0x20] = 0;
    memcpy(imFuji.RAFVersion, PrivateMknBuf.data() + 0x32, 4);
    imFuji.RAFVersion[4] = 0;

    PrivateOrder = PrivateMknBuf.sget2(0);
    unsigned s, l;
    s = ifd_start = PrivateMknBuf.sget4(2)+6;
    l = ifd_len = PrivateMknBuf.sget4(ifd_start);

	if (!PrivateMknBuf.sget4(ifd_start+ifd_len+4))
      FujiShotSelect = 0;

    if ((FujiShotSelect == 1) && (PrivateMknLength > ifd_len*2)) {
      ifd_start += (ifd_len+4);
      ifd_len = PrivateMknBuf.sget4(ifd_start);
      if ((ifd_start+ifd_len) > PrivateMknLength) {
        ifd_start = s;
        ifd_len = l;
        FujiShotSelect = 0;
      }
    } else FujiShotSelect = 0;

    PrivateEntries = PrivateMknBuf.sget4(ifd_start + 4);
    if ((PrivateEntries > 1000) ||
        ((PrivateOrder != 0x4d4d) && (PrivateOrder != 0x4949)))
    {
      return;
    }
    posPrivateMknBuf = (ifd_start+8);

    /*
     * because Adobe DNG converter strips or misplaces 0xfnnn tags,
     * for now, Auto WB is missing for the following cameras:
     * - F550EXR / F600EXR / F770EXR / F800EXR / F900EXR
     * - HS10 / HS11 / HS20EXR / HS30EXR / HS33EXR / HS35EXR / HS50EXR
     * - S1 / SL1000
     **/
    while (PrivateEntries--)
    {
	  PrivateMknBuf.set_order(0x4d4d);
	  order = 0x4d4d;
      PrivateTagID = PrivateMknBuf.sget2(posPrivateMknBuf);
      PrivateTagBytes = PrivateMknBuf.sget2(posPrivateMknBuf + 2);
      posPrivateMknBuf += 4;
	  PrivateMknBuf.set_order(PrivateOrder);
	  order = PrivateOrder;

	  if (PrivateTagID >= 0x2000 && PrivateTagID <= 0x2410)
	  {
		  for(int q = 0; q < tag2wbtable_size; q++)
			  if (tag2wbtable[q].tag == PrivateTagID)
			  {
				  get_average_WB(tag2wbtable[q].wb);
				  break;
			  }
	  }
      else if (PrivateTagID == 0x2f00)
      {
        int nWBs = MIN(PrivateMknBuf.sget4(posPrivateMknBuf), 6);
        posWB = posPrivateMknBuf + 4;
        for (int wb_ind = LIBRAW_WBI_Custom1; wb_ind < LIBRAW_WBI_Custom1+nWBs; wb_ind++) {
          FORC4 icWBC[wb_ind][GRGB_2_RGBG(c)] =
			  PrivateMknBuf.sget2(posWB + (c << 1));
          if ((PrivateTagBytes >= unsigned(4+16*nWBs)) && average_WBData) {
            posWB += 8;
            FORC4 icWBC[wb_ind][GRGB_2_RGBG(c)] =
                    (icWBC[wb_ind][GRGB_2_RGBG(c)] +
						PrivateMknBuf.sget2(posWB + (c << 1))) /2;
          }
          if (use_WBcorr_coeffs) {
             icWBC[wb_ind][0] = int(icWBC[wb_ind][0]*wbR_corr);
             icWBC[wb_ind][2] = int(icWBC[wb_ind][2]*wbB_corr);
          }
          posWB += 8;
        }
      }
      else if (PrivateTagID == 0x2ff0)
      {
        get_average_WB(LIBRAW_WBI_AsShot);
        FORC4 cam_mul[c] = float(icWBC[LIBRAW_WBI_AsShot][c]);
      }
      else if ((PrivateTagID == 0x4000) &&
               ((PrivateTagBytes == 8) || (PrivateTagBytes == 16)))
      {
        imFuji.BlackLevel[0] = PrivateTagBytes / 2;
        FORC4 imFuji.BlackLevel[GRGB_2_RGBG(c)+1] =
			PrivateMknBuf.sget2(posPrivateMknBuf + (c << 1));
        if (imFuji.BlackLevel[0] == 8) {
          FORC4 imFuji.BlackLevel[GRGB_2_RGBG(c) + 5] =
			  PrivateMknBuf.sget2(posPrivateMknBuf + (c << 1) + 8);
        }
      }
      else if (PrivateTagID == 0x9650)
      {
        short a = (short)PrivateMknBuf.sget2(posPrivateMknBuf);
        float b = fMAX(1.0f, PrivateMknBuf.sget2(posPrivateMknBuf + 2));
        imFuji.ExpoMidPointShift = a / b;
        imCommon.ExposureCalibrationShift += imFuji.ExpoMidPointShift;
      }
      else if ((PrivateTagID == 0xc000) && (PrivateTagBytes > 3) &&
               (PrivateTagBytes < 10240000))
      {
		PrivateMknBuf.set_order(0x4949);
		order = 0x4949;
        if (PrivateTagBytes != 4096) // not one of Fuji X-A3, X-A5, X-A7, X-A10, X-A20, X-T100, X-T200, XF10
        {
          int is34 = 0;
		  PrivateMknBuf.checkoffset(posPrivateMknBuf + 8); // will raise exception if no space
          guess_RAFDataGeneration (PrivateMknBuf.data() + posPrivateMknBuf);

// printf (">> ID: 0x%llx, RAFDataVersion: 0x%04x, RAFDataGeneration: %d\n",
// ilm.CamID, imFuji.RAFDataVersion, imFuji.RAFDataGeneration);

          for (posWB = 0; posWB < (int)PrivateTagBytes - 16; posWB++)
          {
            PrivateMknBuf.checkoffset(posWB + 12); // will raise exception if no space
            if ((!memcmp(PrivateMknBuf.data() + posWB, "TSNERDTS", 8) &&
                 (PrivateMknBuf.sget2( posWB + 10) > 125)))
            {
              posWB += 10;
              icWBC[LIBRAW_WBI_Auto][1] =
                  icWBC[LIBRAW_WBI_Auto][3] =
				  PrivateMknBuf.sget2(posWB);
              icWBC[LIBRAW_WBI_Auto][0] =
				  PrivateMknBuf.sget2(posWB + 2);
              icWBC[LIBRAW_WBI_Auto][2] =
				  PrivateMknBuf.sget2(posWB + 4);
              break;
            }
          }

          if ((imFuji.RAFDataVersion == 0x0260) || // X-Pro3, GFX 100S
              (imFuji.RAFDataVersion == 0x0261) || // X100V, GFX 50S II
              (imFuji.RAFDataVersion == 0x0262) || // X-T4
              (imFuji.RAFDataVersion == 0x0263) || // X-H2s
              (imFuji.RAFDataVersion == 0x0264) || // X-S10, X-H2
              (imFuji.RAFDataVersion == 0x0265) || // X-E4, X-T5
              (imFuji.RAFDataVersion == 0x0266) || // X-T30 II
              (imFuji.RAFDataVersion == 0x0267) || // GFX 100 II
              (imFuji.RAFDataVersion == 0x0369) || // X100VI
                !strcmp(model, "X-Pro3")     ||
                !strcmp(model, "GFX 100 II") || !strcmp(model, "GFX100 II")  ||
                !strcmp(model, "GFX 100S")   || !strcmp(model, "GFX100S")    ||
                !strcmp(model, "GFX 50S II") || !strcmp(model, "GFX50S II")  ||
                !strcmp(model, "X100VI")     ||
                !strcmp(model, "X100V")      ||
                !strcmp(model, "X-H2")       ||
                !strcmp(model, "X-H2S")      ||
                !strcmp(model, "X-T4")       ||
                !strcmp(model, "X-T5")       ||
                !strcmp(model, "X-E4")       ||
                !strcmp(model, "X-T30 II")   ||
                !strcmp(model, "X-S10"))
            is34 = 1;

          if (imFuji.RAFDataVersion == 0x4500) // X-E1, RAFData gen. 3
          {
            wb_section_offset = 0x13ac;
          }
          else if (imFuji.RAFDataVersion == 0x0146 || // X20
                   imFuji.RAFDataVersion == 0x0149 || // X100S
                   imFuji.RAFDataVersion == 0x0249)   // X100S
          {
            wb_section_offset = 0x1410;
          }
          else if (imFuji.RAFDataVersion == 0x014d || // X-M1
                   imFuji.RAFDataVersion == 0x014e)   // X-A1, X-A2
          {
            wb_section_offset = 0x1474;
          }
          else if (imFuji.RAFDataVersion == 0x014f || // X-E2
                   imFuji.RAFDataVersion == 0x024f || // X-E2
                   imFuji.RAFDataVersion == 0x025d || // X-H1
                   imFuji.RAFDataVersion == 0x035d)   // X-H1
          {
            wb_section_offset = 0x1480;
          }
          else if (imFuji.RAFDataVersion == 0x0150) // XQ1, XQ2
          {
            wb_section_offset = 0x1414;
          }
          else if (imFuji.RAFDataVersion == 0x0151 || // X-T1 w/diff. fws
                   imFuji.RAFDataVersion == 0x0251 || imFuji.RAFDataVersion == 0x0351 ||
                   imFuji.RAFDataVersion == 0x0451 || imFuji.RAFDataVersion == 0x0551)
          {
            wb_section_offset = 0x14b0;
          }
          else if (imFuji.RAFDataVersion == 0x0152 || // X30
                   imFuji.RAFDataVersion == 0x0153)   // X100T
          {
            wb_section_offset = 0x1444;
          }
          else if (imFuji.RAFDataVersion == 0x0154) // X-T10
          {
            wb_section_offset = 0x1824;
          }
          else if (imFuji.RAFDataVersion == 0x0155) // X70
          {
            wb_section_offset = 0x17b4;
          }
          else if (imFuji.RAFDataVersion == 0x0255 || // X-Pro2
                   imFuji.RAFDataVersion == 0x0455)
          {
            wb_section_offset = 0x135c;
          }
          else if (imFuji.RAFDataVersion == 0x0258 || // X-T2
                   imFuji.RAFDataVersion == 0x025b)   // X-T20
          {
            wb_section_offset = 0x13dc;
          }
          else if (imFuji.RAFDataVersion == 0x0259) // X100F
          {
            wb_section_offset = 0x1370;
          }
          else if (imFuji.RAFDataVersion == 0x025a || // GFX 50S
                   imFuji.RAFDataVersion == 0x045a)
          {
            wb_section_offset = 0x1424;
         }
          else if (imFuji.RAFDataVersion == 0x025c) // X-E3
          {
            wb_section_offset = 0x141c;
          }
          else if (imFuji.RAFDataVersion == 0x025e) // X-T3
          {
            wb_section_offset = 0x2014;
          }
          else if (imFuji.RAFDataVersion == 0x025f) // X-T30, GFX 50R, GFX 100 (? RAFDataVersion 0x045f)
          {
            if (!strcmp(model, "X-T30")) {
              if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20b8))
                wb_section_offset = 0x20b8;
              else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20c8))
					  wb_section_offset = 0x20c8;
            }
            else if (!strcmp(model, "GFX 50R"))
              wb_section_offset = 0x1424;
            else if (!strcmp(model, "GFX 100"))
              wb_section_offset = 0x20e4;
          }
          else if (imFuji.RAFDataVersion == 0x0260) // X-Pro3, GFX 100S
          {
           if (!strcmp(model, "X-Pro3"))
              wb_section_offset = 0x20e8;
            else if (!strcmp(model, "GFX 100S") || !strcmp(model, "GFX100S"))
              wb_section_offset = 0x2108;
          }
          else if (imFuji.RAFDataVersion == 0x0261) // X100V, GFX 50S II
          {
            if (!strcmp(model, "X100V"))
              wb_section_offset = 0x2078;
            else if (!strcmp(model, "GFX 50S II") || !strcmp(model, "GFX50S II"))
              wb_section_offset = 0x214c;
          }
          else if (imFuji.RAFDataVersion == 0x0262) // X-T4
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21c8))
              wb_section_offset = 0x21c8;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21cc))
              wb_section_offset = 0x21cc;
          }
          else if (imFuji.RAFDataVersion == 0x0263) // X-H2S
          {
            wb_section_offset = 0x0b40;
          }
          else if (imFuji.RAFDataVersion == 0x0264) // X-S10, X-H2
          {
            if (!strcmp(model, "X-S10"))
              wb_section_offset = 0x21de;
            else if (!strcmp(model, "X-H2"))
              wb_section_offset = 0x0b3e;
          }
          else if ((imFuji.RAFDataVersion == 0x0265)  || // X-E4, X-T5
                   (imFuji.RAFDataVersion == 0x0266))    // X-T30 II, X-S20
          {
            if (!strcmp(model, "X-T5") ||
                !strcmp(model, "X-S20"))
              wb_section_offset = 0x0c72;
            else
              wb_section_offset = 0x21cc;
          }
          else if (imFuji.RAFDataVersion == 0x0267) // GFX 100 II
          {
            wb_section_offset = 0x0cae;
          }
          else if (imFuji.RAFDataVersion == 0x0355) // X-E2S
          {
            wb_section_offset = 0x1840;
          }
          else if (imFuji.RAFDataVersion == 0x0369) // X100VI
          {
            wb_section_offset = 0x0c5a;
          }

/* try for unknown RAF Data versions */
          else if (!strcmp(model, "X-Pro2"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x135c))
              wb_section_offset = 0x135c;
          }
          else if (!strcmp(model, "X100F"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1370))
              wb_section_offset = 0x1370;
          }
          else if (!strcmp(model, "X-E1"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x13ac))
              wb_section_offset = 0x13ac;
          }
          else if (!strcmp(model, "X-T2") ||
                   !strcmp(model, "X-T20"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x13dc))
              wb_section_offset = 0x13dc;
          }
          else if (!strcmp(model, "X20") ||
                   !strcmp(model, "X100S"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1410))
              wb_section_offset = 0x1410;
          }
          else if (!strcmp(model, "XQ1") ||
                   !strcmp(model, "XQ2"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1414))
              wb_section_offset = 0x1414;
          }
          else if (!strcmp(model, "X-E3"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x141c))
              wb_section_offset = 0x141c;
          }
          else if (!strcmp(model, "GFX 50S") ||
                   !strcmp(model, "GFX 50R"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1424))
              wb_section_offset = 0x1424;
          }
          else if (!strcmp(model, "GFX 50S II") ||
                   !strcmp(model, "GFX50S II")) {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x214c))
              wb_section_offset = 0x214c;
          }
          else if (!strcmp(model, "X30") ||
                   !strcmp(model, "X100T"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1444))
              wb_section_offset = 0x1444;
          }
          else if (!strcmp(model, "X-M1") ||
                   !strcmp(model, "X-A1") ||
                   !strcmp(model, "X-A2"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1474))
              wb_section_offset = 0x1474;
          }
          else if (!strcmp(model, "X-E2") ||
                   !strcmp(model, "X-H1"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1480))
              wb_section_offset = 0x1480;
          }
          else if (!strcmp(model, "X-T1"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x14b0))
              wb_section_offset = 0x14b0;
          }
          else if (!strcmp(model, "X70"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x17b4))
              wb_section_offset = 0x17b4;
          }
          else if (!strcmp(model, "X-T10"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1824))
              wb_section_offset = 0x1824;
          }
          else if (!strcmp(model, "X-E2S"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1840))
              wb_section_offset = 0x1840;
          }
          else if (!strcmp(model, "X-T3"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x2014))
              wb_section_offset = 0x2014;
          }
          else if (!strcmp(model, "X100VI"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x0c5a))
              wb_section_offset = 0x0c5a;
          }
          else if (!strcmp(model, "X100V"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x2078))
              wb_section_offset = 0x2078;
          }
          else if (!strcmp(model, "X-T30"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20b8))
              wb_section_offset = 0x20b8;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20c8))
              wb_section_offset = 0x20c8;
          }
          else if (!strcmp(model, "GFX 100"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20e4))
              wb_section_offset = 0x20e4;
          }
          else if (!strcmp(model, "X-Pro3"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x20e8))
              wb_section_offset = 0x20e8;
          }
          else if (!strcmp(model, "GFX100S") ||
                   !strcmp(model, "GFX 100S"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x2108))
              wb_section_offset = 0x2108;
          }
          else if (!strcmp(model, "X-T4"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21c8))
              wb_section_offset = 0x21c8;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21cc))
              wb_section_offset = 0x21cc;
          }
          else if ((!strcmp(model, "X-E4"))       ||
                   (!strcmp(model, "X-T30 II")))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21cc))
              wb_section_offset = 0x21cc;
          }
          else if (!strcmp(model, "X-S10"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x21de))
              wb_section_offset = 0x21de;
          }
          else if (!strcmp(model, "X-H2"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x0b3e))
              wb_section_offset = 0x0b3e;
          }
          else if (!strcmp(model, "X-H2S"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x0b40))
              wb_section_offset = 0x0b40;
          }
          else if (!strcmp(model, "X-T5") ||
                   !strcmp(model, "X-S20"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x0c72))
              wb_section_offset = 0x0c72;
          }
          else if (!strcmp(model, "GFX 100 II") ||
                   !strcmp(model, "GFX100 II"))
          {
            if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x0cae))
              wb_section_offset = 0x0cae;
          }

/* no RAF Data version for the models below */
          else if (!strcmp(model, "FinePix X100")) // X100 0 0x19f0 0x19e8
          {
            if (!strcmp(imFuji.RAFVersion, "0069"))
              wb_section_offset = 0x19e8;
            else if (!strcmp(imFuji.RAFVersion, "0100") ||
                     !strcmp(imFuji.RAFVersion, "0110"))
              wb_section_offset = 0x19f0;
			else
			{
				if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x19e8))
					wb_section_offset = 0x19e8;
                else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x19f0))
                  wb_section_offset = 0x19f0;
			}
          }
          else if (!strcmp(model, "X-Pro1")) // X-Pro1 0 0x13a4
          {
            if (!strcmp(imFuji.RAFVersion, "0100") ||
                !strcmp(imFuji.RAFVersion, "0101") ||
                !strcmp(imFuji.RAFVersion, "0204"))
              wb_section_offset = 0x13a4;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x13a4))
              wb_section_offset = 0x13a4;
          }
          else if (!strcmp(model, "XF1")) // XF1 0 0x138c
          {
            if (!strcmp(imFuji.RAFVersion, "0100"))
              wb_section_offset = 0x138c;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x138c))
              wb_section_offset = 0x138c;
          }
          else if (!strcmp(model, "X-S1")) // X-S1 0 0x1284
          {
            if (!strcmp(imFuji.RAFVersion, "0100"))
              wb_section_offset = 0x1284;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1284))
              wb_section_offset = 0x1284;
          }
          else if (!strcmp(model, "X10")) // X10 0 0x1280 0x12d4
          {
            if (!strcmp(imFuji.RAFVersion, "0100") ||
                !strcmp(imFuji.RAFVersion, "0102"))
              wb_section_offset = 0x1280;
            else if (!strcmp(imFuji.RAFVersion, "0103"))
              wb_section_offset = 0x12d4;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x1280))
					wb_section_offset = 0x1280;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x12d4))
              wb_section_offset = 0x12d4;
          }
          else if (!strcmp(model, "XF1")) // XF1 0 0x138c
          {
            if (!strcmp(imFuji.RAFVersion, "0100"))
              wb_section_offset = 0x138c;
            else if (PrivateMknBuf.isWB(posPrivateMknBuf + 0x138c))
              wb_section_offset = 0x138c;
          }

          if (wb_section_offset && PrivateMknBuf.isWB(posPrivateMknBuf + wb_section_offset))
          {

            if (!imFuji.RAFDataVersion)
            {
              posWB = posPrivateMknBuf + wb_section_offset - 6;
              icWBC[LIBRAW_WBI_Auto][1] = icWBC[LIBRAW_WBI_Auto][3] = PrivateMknBuf.sget2(posWB);
              icWBC[LIBRAW_WBI_Auto][0] = PrivateMknBuf.sget2(posWB + 2);
              icWBC[LIBRAW_WBI_Auto][2] = PrivateMknBuf.sget2(posWB + 4);
            }

            posWB = posPrivateMknBuf + wb_section_offset;
            for (int wb_ind = 0; wb_ind < (int)Fuji_wb_list1.size(); posWB += 6, wb_ind++)
            {
              icWBC[Fuji_wb_list1[wb_ind]][1] = icWBC[Fuji_wb_list1[wb_ind]][3] = PrivateMknBuf.sget2(posWB);
              icWBC[Fuji_wb_list1[wb_ind]][0] = PrivateMknBuf.sget2(posWB + 2);
              icWBC[Fuji_wb_list1[wb_ind]][2] = PrivateMknBuf.sget2(posWB + 4);
            }
            int found = 0;
            if (is34)
              posWB += 0x30;
            posWB += 0xc0;
            ushort Gval = PrivateMknBuf.sget2(posWB);
            for (int posEndCCTsection = posWB; posEndCCTsection < (posWB + 30);
                 posEndCCTsection += 6)
            {
              if (PrivateMknBuf.sget2(posEndCCTsection) != Gval)
              {
                if (is34)
                  wb_section_offset = posEndCCTsection - 34*3*2; // 34 records, 3 2-byte values in a record
                else
                  wb_section_offset = posEndCCTsection - 31*3*2; // 31 records, 3 2-byte values in a record
                found = 1;
                break;
              }
            }

            if (found)
            {
              for (int iCCT = 0; iCCT < 31; iCCT++)
              {
                icWBCCTC[iCCT][0] = float(FujiCCT_K[iCCT]);
                icWBCCTC[iCCT][1] = PrivateMknBuf.sget2(wb_section_offset + iCCT * 6 + 2);
                icWBCCTC[iCCT][2] = icWBCCTC[iCCT][4] = PrivateMknBuf.sget2(wb_section_offset + iCCT * 6);
                icWBCCTC[iCCT][3] = PrivateMknBuf.sget2(wb_section_offset + iCCT * 6 + 4);
              }
            }
          }
        }
        else // process 4K raf data
        {
          int wb[4];
          int nWB, tWB, pWB;
          int iCCT = 0;
          imFuji.RAFDataGeneration = 4096; // X-A3, X-A5, X-A7, X-A10, X-A20, X-T100, X-T200, XF10
          posWB = posPrivateMknBuf + 0x200;
          for (int wb_ind = 0; wb_ind < 42; wb_ind++)
          {
            nWB = PrivateMknBuf.sget4(posWB);
            posWB += 4;
            tWB = PrivateMknBuf.sget4(posWB);
            posWB += 4;
            wb[0] = PrivateMknBuf.sget4(posWB) << 1;
            posWB += 4;
            wb[1] = PrivateMknBuf.sget4(posWB);
            posWB += 4;
            wb[3] = PrivateMknBuf.sget4(posWB);
            posWB += 4;
            wb[2] = PrivateMknBuf.sget4(posWB) << 1;
            posWB += 4;

            if (tWB && (iCCT < 64))
            {
              icWBCCTC[iCCT][0] = float(tWB);
              FORC4 icWBCCTC[iCCT][c + 1] = float(wb[c]);
              iCCT++;
            }
            if (nWB != 0x46)
            {
              for (pWB = 1; pWB < (int)Fuji_wb_list2.size(); pWB += 2)
              {
                if (Fuji_wb_list2[pWB] == nWB)
                {
                  FORC4 icWBC[Fuji_wb_list2[pWB - 1]][c] = wb[c];
                  break;
                }
              }
            }
          }
        }
      }
      posPrivateMknBuf += PrivateTagBytes;
    }
  }
#undef get_average_WB
}

void LibRaw::parseFujiMakernotes(unsigned tag, unsigned type, unsigned len,
                                 unsigned /*dng_writer*/)
{
  if (tag == 0x0010)
  {
    char FujiSerial[sizeof(imgdata.shootinginfo.InternalBodySerial)];
	char *words[4] = { 0,0,0,0 };
    char yy[2], mm[3], dd[3], ystr[16], ynum[16];
    int year, nwords, ynum_len;
    unsigned c;
    memset(FujiSerial, 0, sizeof(imgdata.shootinginfo.InternalBodySerial));
    ifp->read(FujiSerial, MIN(len,sizeof(FujiSerial)), 1);
    nwords = getwords(FujiSerial, words, 4,
                      sizeof(imgdata.shootinginfo.InternalBodySerial));
    for (int i = 0; i < nwords; i++)
    {
	  if (!words[i]) break;  // probably damaged input
      mm[2] = dd[2] = 0;
      if (strnlen(words[i],
                  sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) < 18)
      {
        if (i == 0)
        {
          strncpy(imgdata.shootinginfo.InternalBodySerial, words[0],
                  sizeof(imgdata.shootinginfo.InternalBodySerial) - 1);
        }
        else
        {
          char tbuf[sizeof(imgdata.shootinginfo.InternalBodySerial)];
          snprintf(tbuf, sizeof(tbuf)-1, "%s %s",
                   imgdata.shootinginfo.InternalBodySerial, words[i]);
          strncpy(imgdata.shootinginfo.InternalBodySerial, tbuf,
                  sizeof(imgdata.shootinginfo.InternalBodySerial) - 1);
        }
      }
      else
      {
        strncpy(
            dd,
            words[i] +
                strnlen(words[i],
                        sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) -
                14,
            2);
        strncpy(
            mm,
            words[i] +
                strnlen(words[i],
                        sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) -
                16,
            2);
        strncpy(
            yy,
            words[i] +
                strnlen(words[i],
                        sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) -
                18,
            2);
        year = (yy[0] - '0') * 10 + (yy[1] - '0');
        if (year < 70)
          year += 2000;
        else
          year += 1900;

        ynum_len = MIN(
            int(sizeof(ynum) - 1),
            (int)strnlen(words[i],
                         sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) -
                18);
        strncpy(ynum, words[i], ynum_len);
        ynum[ynum_len] = 0;
        for (int j = 0; ynum[j] && ynum[j + 1] && sscanf(ynum + j, "%2x", &c);
             j += 2)
          ystr[j / 2] = c;
        ynum_len /= 2;
        ystr[ynum_len + 1] = 0;
        strcpy(model2, ystr);

        if (i == 0)
        {
          char tbuf[sizeof(imgdata.shootinginfo.InternalBodySerial)];

          if (nwords == 1)
          {
            snprintf(
              tbuf, sizeof(tbuf), "%s %d:%s:%s %s",
              ystr, year, mm, dd,
              words[0] +
                strnlen(words[0], sizeof(imgdata.shootinginfo.InternalBodySerial)-1)-12);
          }
          else
          {
            snprintf(
                tbuf, sizeof(tbuf), "%s %d:%s:%s %s", ystr, year, mm, dd,
                words[0] +
                    strnlen(words[0],
                            sizeof(imgdata.shootinginfo.InternalBodySerial) -
                                1) -
                    12);
          }
          strncpy(imgdata.shootinginfo.InternalBodySerial, tbuf,
                  sizeof(imgdata.shootinginfo.InternalBodySerial) - 1);
        }
        else
        {
          char tbuf[sizeof(imgdata.shootinginfo.InternalBodySerial)];
          snprintf(
              tbuf, sizeof(tbuf), "%s %s %d:%s:%s %s",
              imgdata.shootinginfo.InternalBodySerial, ystr, year, mm, dd,
              words[i] +
                  strnlen(words[i],
                          sizeof(imgdata.shootinginfo.InternalBodySerial) - 1) -
                  12);
          strncpy(imgdata.shootinginfo.InternalBodySerial, tbuf,
                  sizeof(imgdata.shootinginfo.InternalBodySerial) - 1);
        }
      }
    }
  }
  else
    switch (tag)
    {
    case 0x1002:
      imFuji.WB_Preset = get2();
      break;
    case 0x1011:
      imCommon.FlashEC = getrealf(type);
      break;
    case 0x1020:
      imFuji.Macro = get2();
      break;
    case 0x1021:
      imFuji.FocusMode = imgdata.shootinginfo.FocusMode = get2();
      break;
    case 0x1022:
      imFuji.AFMode = get2();
      break;
    case 0x1023:
      imFuji.FocusPixel[0] = get2();
      imFuji.FocusPixel[1] = get2();
      break;
    case 0x102b:
      imFuji.PrioritySettings = get2();
      break;
    case 0x102d:
      imFuji.FocusSettings = get4();
      break;
    case 0x102e:
      imFuji.AF_C_Settings = get4();
      break;
    case 0x1034:
      imFuji.ExrMode = get2();
      break;
    case 0x104d:
      FujiCropMode = get2(); // odd: one of raw dimensions here can be lost
      break;
    case 0x1050:
      imFuji.ShutterType = get2();
      break;
    case 0x1100:
      imFuji.AutoBracketing = get2(); // AutoBracketing = 6 for pixel shift mode
      break;
    case 0x1101:
      imFuji.SequenceNumber = get2();
      break;
    case 0x1103:
      imgdata.shootinginfo.DriveMode = get2();
      imFuji.DriveMode = imgdata.shootinginfo.DriveMode & 0xff;
      break;
    case 0x1105:
      imFuji.SeriesLength = get2();
      break;
    case 0x1106:
      imFuji.PixelShiftOffset[0] = getrealf(type);
      imFuji.PixelShiftOffset[1] = getrealf(type);
      break;
    case 0x1301:
      imFuji.FocusWarning = get2();
      break;
    case 0x1400:
      imFuji.DynamicRange = get2();
      break;
    case 0x1401:
      imFuji.FilmMode = get2();
      break;
    case 0x1402:
      imFuji.DynamicRangeSetting = get2();
      break;
    case 0x1403:
      imFuji.DevelopmentDynamicRange = get2();
      break;
    case 0x1404:
      ilm.MinFocal = getrealf(type);
      break;
    case 0x1405:
      ilm.MaxFocal = getrealf(type);
      break;
    case 0x1406:
      ilm.MaxAp4MinFocal = getrealf(type);
      break;
    case 0x1407:
      ilm.MaxAp4MaxFocal = getrealf(type);
      break;
    case 0x140b:
      imFuji.AutoDynamicRange = get2();
      break;
    case 0x1422:
      imFuji.ImageStabilization[0] = get2();
      imFuji.ImageStabilization[1] = get2();
      imFuji.ImageStabilization[2] = get2();
      imgdata.shootinginfo.ImageStabilization =
          (imFuji.ImageStabilization[0] << 9) + imFuji.ImageStabilization[1];
      break;
    case 0x1438:
      imFuji.ImageCount = get2();
      break;
    case 0x1431:
      imFuji.Rating = get4();
      break;
    case 0x1443:
      imFuji.DRangePriority = get2();
      break;
    case 0x1444:
      imFuji.DRangePriorityAuto = get2();
      break;
    case 0x1445:
      imFuji.DRangePriorityFixed = get2();
      break;
    case 0x1447:
      stmread(imFuji.FujiModel, len, ifp);
      break;
    case 0x1448:
      stmread(imFuji.FujiModel2, len, ifp);
      break;
    }
  return;
}

void LibRaw::parse_fuji_thumbnail(INT64 offset)
{
    uchar xmpmarker[] = "http://ns.adobe.com/xap/1.0/";
    uchar buf[sizeof(xmpmarker)+1];
    int xmpsz = sizeof(xmpmarker); // we do not

    INT64 pos = ftell(ifp);
    fseek(ifp, offset, SEEK_SET);
    ushort s_order = order;
    order = 0x4a4a; // JPEG is always in MM order

    if (get2() == 0xFFD8)
    {
        while (1)
        {
          ushort tag = get2();
          if (tag != 0xFFE1 && tag != 0xFFE2) // allow APP1/APP2 only
            break;
          INT64 tpos = ftell(ifp);
          int len = get2();
          if (len > xmpsz + 2)
          {
              if ((fread(buf, 1, xmpsz, ifp) == xmpsz) && !memcmp(buf, xmpmarker, xmpsz)) // got it
              {
                  xmplen = len - xmpsz - 2;
                  xmpdata = (char*) calloc(xmplen+1,1);
                  unsigned br = fread(xmpdata, 1, xmplen, ifp);
                  xmpdata[br] = 0;
                  break;
              }
          }
          fseek(ifp, tpos + len, SEEK_SET);
        }
    }

    order = s_order;
    fseek(ifp, pos, SEEK_SET);
}

void LibRaw::parse_fuji(INT64 offset)
{
  unsigned entries, tag, len, c;
  INT64 save;

#define get_average_WB(wb_index)                                               \
  do {                                                                         \
	  FORC4 icWBC[wb_index][GRGB_2_RGBG(c)] = get2();                              \
	  if ((len == 16) && average_WBData) {                                         \
		FORC4 icWBC[wb_index][GRGB_2_RGBG(c)] =                                    \
				 (icWBC[wb_index][GRGB_2_RGBG(c)] + get2())/2;                     \
	  }                                                                            \
	  if (use_WBcorr_coeffs) {                                                     \
		icWBC[wb_index][0] = int(icWBC[wb_index][0] * wbR_corr);                   \
		icWBC[wb_index][2] = int( icWBC[wb_index][2] * wbB_corr);                  \
	  }                                                                            \
  } while(0)

  ushort raw_inset_present = 0;
  ushort use_WBcorr_coeffs = 0;
  double wbR_corr = 1.0;
  double wbB_corr = 1.0;
  ilm.CamID = unique_id;
  int average_WBData = 1;

  fseek(ifp, offset, SEEK_SET);
  entries = get4();
  if (entries > 255)
    return;
  imgdata.process_warnings |= LIBRAW_WARN_PARSEFUJI_PROCESSED;

  if (strstr(model, "S2Pro")
      || strstr(model, "S20Pro")
      || strstr(model, "F700")
      || strstr(model, "S5000")
      || strstr(model, "S7000")
      ) {
    use_WBcorr_coeffs = 1;
    wbR_corr = 10.0 / 17.0 / 0.652941;
    wbB_corr = 2.0 /3.0 / (3.0 / 4.0 + 1.0 / 300.0);
  } else if (strstr(model, "DBP") || strstr(model, "DX-2000")) {
    use_WBcorr_coeffs = 1;
    wbR_corr = 0.7632653061;
    wbB_corr = 0.8591549296;
  }

  while (entries--)
  {
    tag = get2();
    len = get2();
    save = ftell(ifp);
    if (tag == 0x0100) // RawImageFullSize
    {
      raw_height = get2();
      raw_width = get2();
      raw_inset_present = 1;
    }
    else if ((tag == 0x0110) && raw_inset_present) // RawImageCropTopLeft
    {
      imgdata.sizes.raw_inset_crops[0].ctop = get2();
      imgdata.sizes.raw_inset_crops[0].cleft = get2();
    }
    else if ((tag == 0x0111) && raw_inset_present) // RawImageCroppedSize
    {
      imgdata.sizes.raw_inset_crops[0].cheight = get2();
      imgdata.sizes.raw_inset_crops[0].cwidth = get2();
    }
    else if ((tag == 0x0115) && raw_inset_present) // RawImageAspectRatio
    {
      int a = get2();
      int b = get2();
      if (a * b == 6)
        imgdata.sizes.raw_aspect = LIBRAW_IMAGE_ASPECT_3to2;
      else if (a * b == 12)
        imgdata.sizes.raw_aspect = LIBRAW_IMAGE_ASPECT_4to3;
      else if (a * b == 144)
        imgdata.sizes.raw_aspect = LIBRAW_IMAGE_ASPECT_16to9;
      else if (a * b == 1)
        imgdata.sizes.raw_aspect = LIBRAW_IMAGE_ASPECT_1to1;
    }
    else if (tag == 0x0121) // RawImageSize
    {
      height = get2();
      if ((width = get2()) == 4284)
        width += 3;
    }
    else if (tag == 0x0130) // FujiLayout,
    {
      fuji_layout = fgetc(ifp) >> 7;
      fuji_width  = !(fgetc(ifp) & 8);
    }
    else if (tag == 0x0131) // XTransLayout
    {
      filters = 9;
      char *xtrans_abs_alias = &xtrans_abs[0][0];
      FORC(36)
      {
        int q = fgetc(ifp);
        xtrans_abs_alias[35 - c] = MAX(0, MIN(q, 2)); /* & 3;*/
      }
    }
    else if (tag == 0x2ff0) // WB_GRGBLevels
    {
      get_average_WB(LIBRAW_WBI_AsShot);
      FORC4 cam_mul[c] = float(icWBC[LIBRAW_WBI_AsShot][c]);
    }
    else if ((tag == 0x4000) &&
             ((len == 8) || (len == 16)))
    {
      imFuji.BlackLevel[0] = len / 2;
      FORC4 imFuji.BlackLevel[GRGB_2_RGBG(c)+1] = get2();
      if (imFuji.BlackLevel[0] == 8)
        FORC4 imFuji.BlackLevel[GRGB_2_RGBG(c)+5] = get2();
      if (imFuji.BlackLevel[0] == 4)
        FORC4 cblack[c] = imFuji.BlackLevel[c+1];
      else if (imFuji.BlackLevel[0] == 8)
        FORC4 cblack[c] = (imFuji.BlackLevel[c+1]+imFuji.BlackLevel[c+5]) /2;
    }
    else if (tag == 0x9200) // RelativeExposure
    {
      int s1 = get2();
      int s2 = get2();
      if ((s1 == s2) || !s1)
        imFuji.BrightnessCompensation = 0.0f;
      else if ((s1*4) == s2)
        imFuji.BrightnessCompensation = 2.0f;
      else if ((s1*16) == s2)
        imFuji.BrightnessCompensation = 4.0f;
      else
        imFuji.BrightnessCompensation = logf(float(s2)/float(s1))/logf(2.f);
    }
    else if (tag == 0x9650) // RawExposureBias
    {
      short a = (short)get2();
      float b = fMAX(1.0f, get2());
      imFuji.ExpoMidPointShift = a / b;
      imCommon.ExposureCalibrationShift += imFuji.ExpoMidPointShift;
    }
    if (tag >= 0x2000 && tag <= 0x2410)
    {
      for (int q = 0; q < tag2wbtable_size; q++)
        if (tag2wbtable[q].tag == tag)
        {
          get_average_WB(tag2wbtable[q].wb);
          break;
        }
    }
    else if (tag == 0x2f00) // WB_GRGBLevels
    {
      int nWBs = get4();
      nWBs = MIN(nWBs, 6);
      for (int wb_ind = LIBRAW_WBI_Custom1; wb_ind < LIBRAW_WBI_Custom1+nWBs; wb_ind++) {
        FORC4 icWBC[wb_ind][GRGB_2_RGBG(c)] = get2();
        if ((len >= unsigned(4+16*nWBs)) && average_WBData) {
          FORC4 icWBC[wb_ind][GRGB_2_RGBG(c)] =
                  (icWBC[wb_ind][GRGB_2_RGBG(c)] +get2()) /2;
        }
        if (use_WBcorr_coeffs) {
          icWBC[LIBRAW_WBI_Custom1 + wb_ind][0] = int(icWBC[LIBRAW_WBI_Custom1 + wb_ind][0] * wbR_corr);
          icWBC[LIBRAW_WBI_Custom1 + wb_ind][2] = int(icWBC[LIBRAW_WBI_Custom1 + wb_ind][2] * wbB_corr);
        }
      }
    }

    else if (tag == 0xc000) // RAFData
    {
      int offsetWH_inRAFData;
      unsigned save_order = order;
      order = 0x4949;
      if (len > 20000)
      {
        uchar RAFDataHeader[16];
        libraw_internal_data.unpacker_data.posRAFData = save;
        libraw_internal_data.unpacker_data.lenRAFData = (len >> 1);
        fread(RAFDataHeader, sizeof RAFDataHeader, 1, ifp);
        offsetWH_inRAFData = guess_RAFDataGeneration(RAFDataHeader);
        fseek(ifp, offsetWH_inRAFData-int(sizeof RAFDataHeader), SEEK_CUR);
        for (int i=0;
             i< (int)((sizeof imFuji.RAFData_ImageSizeTable) / (sizeof imFuji.RAFData_ImageSizeTable[0]));
             i++) {
          imFuji.RAFData_ImageSizeTable[i] = get4();
        }

//         if ((width > raw_width)
//             || (raw_inset_present && (width < imgdata.sizes.raw_inset_crops[0].cwidth))
//         )
//           width = raw_width;
//         if ((height > raw_height)
//             || (raw_inset_present && (height < imgdata.sizes.raw_inset_crops[0].cheight))
//         )
//           height = raw_height;
//

      }
      else if (len == 4096) // X-A3, X-A5, X-A7, X-A10, X-A20, X-T100, X-T200, XF10
      {                     // Ill.A aligned to CCT 2850
        int wb[4];
        int nWB, tWB;
        int iCCT = 0;
        imFuji.RAFDataGeneration = 4096;
        fseek(ifp, save + 0x200, SEEK_SET);
        for (int wb_ind = 0; wb_ind < 42; wb_ind++)
        {
          nWB = get4();
          tWB = get4();
          wb[0] = get4() << 1;
          wb[1] = get4();
          wb[3] = get4();
          wb[2] = get4() << 1;
          if (tWB && (iCCT < 64))
          {
            icWBCCTC[iCCT][0] = float(tWB);
            FORC4 icWBCCTC[iCCT][c + 1] = float(wb[c]);
            iCCT++;
          }
          if (nWB != 70)
          {
            for (int pWB = 1; pWB < (int)Fuji_wb_list2.size(); pWB += 2)
            {
              if (Fuji_wb_list2[pWB] == nWB)
              {
                FORC4 icWBC[Fuji_wb_list2[pWB - 1]][c] = wb[c];
                break;
              }
            }
          }
        }
      }
      order = save_order;
    }
    fseek(ifp, save + len, SEEK_SET);
  }

  if (!imFuji.RAFDataGeneration) {
    height <<= fuji_layout;
    width >>= fuji_layout;
  }
#undef get_average_WB
}

