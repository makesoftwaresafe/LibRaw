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

void LibRaw::convert_to_rgb()
{
  float out_cam[3][4];
  double num, inverse[3][3];
  static const double(*out_rgb[])[3] = {
      LibRaw_constants::rgb_rgb,  LibRaw_constants::adobe_rgb,
      LibRaw_constants::wide_rgb, LibRaw_constants::prophoto_rgb,
      LibRaw_constants::xyz_rgb,  LibRaw_constants::aces_rgb,
      LibRaw_constants::dcip3d65_rgb,  LibRaw_constants::rec2020_rgb};
  static const char *name[] = {"sRGB",          "Adobe RGB (1998)",
                               "WideGamut D65", "ProPhoto D65",
                               "XYZ",           "ACES",
                               "DCI-P3 D65",    "Rec. 2020"};
  static const unsigned phead[] = {
      1024, 0, 0x2100000,  0x6d6e7472, 0x52474220, 0x58595a20, 0,
      0,    0, 0x61637370, 0,          0,          0x6e6f6e65, 0,
      0,    0, 0,          0xf6d6,     0x10000,    0xd32d};
  unsigned pbody[] = {10,         0x63707274, 0,  36, /* cprt */
                      0x64657363, 0,          60,     /* desc, len is strlen(longest_string) + 12 */
                      0x77747074, 0,          20,     /* wtpt */
                      0x626b7074, 0,          20,     /* bkpt */
                      0x72545243, 0,          14,     /* rTRC */
                      0x67545243, 0,          14,     /* gTRC */
                      0x62545243, 0,          14,     /* bTRC */
                      0x7258595a, 0,          20,     /* rXYZ */
                      0x6758595a, 0,          20,     /* gXYZ */
                      0x6258595a, 0,          20};    /* bXYZ */
  static const unsigned pwhite[] = {0xf351, 0x10000, 0x116cc};
  unsigned pcurve[] = {0x63757276, 0, 1, 0x1000000};

  RUN_CALLBACK(LIBRAW_PROGRESS_CONVERT_RGB, 0, 2);

  gamma_curve(gamm[0], gamm[1], 0, 0);
  memcpy(out_cam, rgb_cam, sizeof out_cam);
  raw_color |= colors == 1 || output_color < 1 || output_color > 8;
  if (!raw_color)
  {
	size_t prof_desc_len;
	std::vector<char> prof_desc;
    int i, j, k;
    prof_desc_len = snprintf(NULL, 0, "%s gamma %g toe slope %g", name[output_color - 1],
                             floor(1000. / gamm[0] + .5) / 1000., floor(gamm[1] * 1000.0 + .5) / 1000.) +
                    1;
    prof_desc.resize(prof_desc_len);
    sprintf(prof_desc.data(), "%s gamma %g toe slope %g", name[output_color - 1], floor(1000. / gamm[0] + .5) / 1000.,
            floor(gamm[1] * 1000.0 + .5) / 1000.);

	oprof = (unsigned *)calloc(phead[0], 1);
    memcpy(oprof, phead, sizeof phead);
    if (output_color == 5)
      oprof[4] = oprof[5];
    oprof[0] = 132 + 12 * pbody[0];
    for (i = 0; i < (int)pbody[0]; i++)
    {
      oprof[oprof[0] / 4] = i ? (i > 1 ? 0x58595a20 : 0x64657363) : 0x74657874;
      pbody[i * 3 + 2] = oprof[0];
      oprof[0] += (pbody[i * 3 + 3] + 3) & -4;
    }
    memcpy(oprof + 32, pbody, sizeof pbody);
    oprof[pbody[5] / 4 + 2] = unsigned(prof_desc_len + 1);
    memcpy((char *)oprof + pbody[8] + 8, pwhite, sizeof pwhite);
    pcurve[3] = (short)(256 / gamm[5] + 0.5) << 16;
    for (i = 4; i < 7; i++)
      memcpy((char *)oprof + pbody[i * 3 + 2], pcurve, sizeof pcurve);
    pseudoinverse((double(*)[3])out_rgb[output_color - 1], inverse, 3);
    for (i = 0; i < 3; i++)
      for (j = 0; j < 3; j++)
      {
        for (num = k = 0; k < 3; k++)
          num += LibRaw_constants::xyzd50_srgb[i][k] * inverse[j][k];
        oprof[pbody[j * 3 + 23] / 4 + i + 2] = unsigned(num * 0x10000 + 0.5);
      }
    for (i = 0; i < (int)phead[0] / 4; i++)
      oprof[i] = htonl(oprof[i]);
    strcpy((char *)oprof + pbody[2] + 8, "auto-generated by dcraw");
    if (pbody[5] + 12 + prof_desc.size() < phead[0])
		strcpy((char *)oprof + pbody[5] + 12, prof_desc.data());
    for (i = 0; i < 3; i++)
      for (j = 0; j < colors; j++)
        for (out_cam[i][j] = 0.f, k = 0; k < 3; k++)
          out_cam[i][j] += float(out_rgb[output_color - 1][i][k] * rgb_cam[k][j]);
  }
  convert_to_rgb_loop(out_cam);

  if (colors == 4 && output_color)
    colors = 3;

  RUN_CALLBACK(LIBRAW_PROGRESS_CONVERT_RGB, 1, 2);
}

void LibRaw::scale_colors()
{
  unsigned bottom, right, size, row, col, ur, uc, i, x, y, c, sum[8];
  int val;
  double dsum[8], dmin, dmax;
  float scale_mul[4], fr, fc;
  ushort *img = 0, *pix;

  RUN_CALLBACK(LIBRAW_PROGRESS_SCALE_COLORS, 0, 2);

  if (user_mul[0])
    memcpy(pre_mul, user_mul, sizeof pre_mul);
  if (use_auto_wb || (use_camera_wb && 
      (cam_mul[0] < -0.5  // LibRaw 0.19 and older: fallback to auto only if cam_mul[0] is set to -1
          || (cam_mul[0] <= 0.00001f  // New default: fallback to auto if no cam_mul parsed from metadata
              && !(imgdata.rawparams.options & LIBRAW_RAWOPTIONS_CAMERAWB_FALLBACK_TO_DAYLIGHT))
          )))
  {
    memset(dsum, 0, sizeof dsum);
    bottom = MIN(greybox[1] + greybox[3], height);
    right = MIN(greybox[0] + greybox[2], width);
    for (row = greybox[1]; row < bottom; row += 8)
      for (col = greybox[0]; col < right; col += 8)
      {
        memset(sum, 0, sizeof sum);
        for (y = row; y < row + 8 && y < bottom; y++)
          for (x = col; x < col + 8 && x < right; x++)
            FORC4
            {
              if (filters)
              {
                c = fcol(y, x);
                val = BAYER2(y, x);
              }
              else
                val = image[y * width + x][c];
              if (val > (int)maximum - 25)
                goto skip_block;
              if ((val -= cblack[c]) < 0)
                val = 0;
              sum[c] += val;
              sum[c + 4]++;
              if (filters)
                break;
            }
        FORC(8) dsum[c] += sum[c];
      skip_block:;
      }
    FORC4 if (dsum[c]) pre_mul[c] = float(dsum[c + 4] / dsum[c]);
  }
  if (use_camera_wb && cam_mul[0] > 0.00001f)
  {
    memset(sum, 0, sizeof sum);
    for (row = 0; row < 8; row++)
      for (col = 0; col < 8; col++)
      {
        c = FC(row, col);
        if ((val = white[row][col] - cblack[c]) > 0)
          sum[c] += val;
        sum[c + 4]++;
      }
    if (imgdata.color.as_shot_wb_applied)
    {
      // Nikon sRAW: camera WB already applied:
      pre_mul[0] = pre_mul[1] = pre_mul[2] = pre_mul[3] = 1.0;
    }
    else if (sum[0] && sum[1] && sum[2] && sum[3])
      FORC4 pre_mul[c] = (float)sum[c + 4] / sum[c];
    else if (cam_mul[0] > 0.00001f && cam_mul[2] > 0.00001f)
      memcpy(pre_mul, cam_mul, sizeof pre_mul);
    else
    {
      imgdata.process_warnings |= LIBRAW_WARN_BAD_CAMERA_WB;
    }
  }
  // Nikon sRAW, daylight
  if (imgdata.color.as_shot_wb_applied && !use_camera_wb && !use_auto_wb &&
      cam_mul[0] > 0.00001f && cam_mul[1] > 0.00001f && cam_mul[2] > 0.00001f)
  {
    for (c = 0; c < 3; c++)
      pre_mul[c] /= cam_mul[c];
  }
  if (pre_mul[1] == 0)
    pre_mul[1] = 1;
  if (pre_mul[3] == 0)
    pre_mul[3] = colors < 4 ? pre_mul[1] : 1;
  if (threshold)
    wavelet_denoise();
  maximum -= black;
  for (dmin = DBL_MAX, dmax = c = 0; c < 4; c++)
  {
    if (dmin > pre_mul[c])
      dmin = pre_mul[c];
    if (dmax < pre_mul[c])
      dmax = pre_mul[c];
  }
  if (!highlight)
    dmax = dmin;
  if (dmax > 0.00001 && maximum > 0)
    FORC4 scale_mul[c] = (pre_mul[c] /= float(dmax)) * 65535.f / maximum;
  else
    FORC4 scale_mul[c] = 1.0;

  if (filters > 1000 && (cblack[4] + 1) / 2 == 1 && (cblack[5] + 1) / 2 == 1)
  {
    FORC4 cblack[FC(c / 2, c % 2)] +=
        cblack[6 + c / 2 % cblack[4] * cblack[5] + c % 2 % cblack[5]];
    cblack[4] = cblack[5] = 0;
  }
  size = iheight * iwidth;
  scale_colors_loop(scale_mul);
  if ((aber[0] != 1 || aber[2] != 1) && colors == 3)
  {
    for (c = 0; c < 4; c += 2)
    {
      if (aber[c] == 1)
        continue;
      img = (ushort *)malloc(size * sizeof *img);
      for (i = 0; i < size; i++)
        img[i] = image[i][c];
      for (row = 0; row < iheight; row++)
      {
        fr = float((row - iheight * 0.5) * aber[c] + iheight * 0.5);
		ur = unsigned(fr);
        if (ur > (unsigned)iheight - 2)
          continue;
        fr -= ur;
        for (col = 0; col < iwidth; col++)
        {
          fc = float((col - iwidth * 0.5) * aber[c] + iwidth * 0.5);
		  uc = unsigned(fc);
          if (uc > (unsigned)iwidth - 2)
            continue;
          fc -= uc;
          pix = img + ur * iwidth + uc;
          image[row * iwidth + col][c] =
			  ushort(
              (pix[0] * (1 - fc) + pix[1] * fc) * (1 - fr) +
              (pix[iwidth] * (1 - fc) + pix[iwidth + 1] * fc) * fr
				  );
        }
      }
      free(img);
    }
  }
  RUN_CALLBACK(LIBRAW_PROGRESS_SCALE_COLORS, 1, 2);
}

// green equilibration
void LibRaw::green_matching()
{
  int i, j;
  double m1, m2, c1, c2;
  int o1_1, o1_2, o1_3, o1_4;
  int o2_1, o2_2, o2_3, o2_4;
  ushort(*img)[4];
  const int margin = 3;
  int oj = 2, oi = 2;
  float f;
  const float thr = 0.01f;
  if (half_size || shrink)
    return;
  if (FC(oj, oi) != 3)
    oj++;
  if (FC(oj, oi) != 3)
    oi++;
  if (FC(oj, oi) != 3)
    oj--;

  img = (ushort(*)[4])calloc(height * width, sizeof *image);
  memcpy(img, image, height * width * sizeof *image);

  for (j = oj; j < height - margin; j += 2)
    for (i = oi; i < width - margin; i += 2)
    {
      o1_1 = img[(j - 1) * width + i - 1][1];
      o1_2 = img[(j - 1) * width + i + 1][1];
      o1_3 = img[(j + 1) * width + i - 1][1];
      o1_4 = img[(j + 1) * width + i + 1][1];
      o2_1 = img[(j - 2) * width + i][3];
      o2_2 = img[(j + 2) * width + i][3];
      o2_3 = img[j * width + i - 2][3];
      o2_4 = img[j * width + i + 2][3];

      m1 = (o1_1 + o1_2 + o1_3 + o1_4) / 4.0;
      m2 = (o2_1 + o2_2 + o2_3 + o2_4) / 4.0;

      c1 = (abs(o1_1 - o1_2) + abs(o1_1 - o1_3) + abs(o1_1 - o1_4) +
            abs(o1_2 - o1_3) + abs(o1_3 - o1_4) + abs(o1_2 - o1_4)) /
           6.0;
      c2 = (abs(o2_1 - o2_2) + abs(o2_1 - o2_3) + abs(o2_1 - o2_4) +
            abs(o2_2 - o2_3) + abs(o2_3 - o2_4) + abs(o2_2 - o2_4)) /
           6.0;
      if ((img[j * width + i][3] < maximum * 0.95) && (c1 < maximum * thr) &&
          (c2 < maximum * thr))
      {
        f = float(image[j * width + i][3] * m1 / m2);
        image[j * width + i][3] = f > 65535.f ? 0xffff : ushort(f);
      }
    }
  free(img);
}
