/*

    File: file_riff.c

    Copyright (C) 1998-2005,2007-2011 Christophe GRENIER <grenier@cgsecurity.org>
  
    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
  
    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <stdio.h>
#include "types.h"
#include "filegen.h"
#include "common.h"
#ifdef DEBUG_RIFF
#include "log.h"
#endif

int data_check_avi_stream(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery);
static void register_header_check_riff(file_stat_t *file_stat);

const file_hint_t file_hint_riff= {
  .extension="riff",
  .description="RIFF audio/video: wav, cdr, avi",
  .min_header_distance=0,
  .max_filesize=PHOTOREC_MAX_FILE_SIZE,
  .recover=1,
  .enable_by_default=1,
  .register_header_check=&register_header_check_riff
};

static const unsigned char riff_header[4]= {'R','I','F','F'};
static const unsigned char rifx_header[4]= {'R','I','F','X'};

typedef struct {
  uint32_t dwFourCC;
  uint32_t dwSize;
//  char data[dwSize];   // contains headers or video/audio data
} riff_chunk_header;

typedef struct {
  uint32_t dwList;
  uint32_t dwSize;
  uint32_t dwFourCC;
//  char data[dwSize-4];
} riff_list_header;

#ifdef DEBUG_RIFF
static void log_riff_list(const uint64_t offset, const unsigned int depth, const riff_list_header *list_header)
{
  unsigned int i;
  log_info("0x%08lx - 0x%08lx ", offset, offset + 8 + le32(list_header->dwSize) - 1);
  for(i = 0; i < depth; i++)
    log_info(" ");
  log_info("%c%c%c%c %c%c%c%c 0x%x\n",
      le32(list_header->dwList),
      le32(list_header->dwList)>>8,
      le32(list_header->dwList)>>16,
      le32(list_header->dwList)>>24,
      le32(list_header->dwFourCC),
      le32(list_header->dwFourCC)>>8,
      le32(list_header->dwFourCC)>>16,
      le32(list_header->dwFourCC)>>24,
      le32(list_header->dwSize));
}

static void log_riff_chunk(const uint64_t offset, const unsigned int depth, const riff_list_header *list_header)
{
  unsigned int i;
  if(le32(list_header->dwSize)==0)
    return ;
  log_info("0x%08lx - 0x%08lx ", offset, offset + 8 + le32(list_header->dwSize) - 1);
  for(i = 0; i < depth; i++)
    log_info(" ");
  log_info("%c%c%c%c 0x%x\n",
      le32(list_header->dwList),
      le32(list_header->dwList)>>8,
      le32(list_header->dwList)>>16,
      le32(list_header->dwList)>>24,
      le32(list_header->dwSize));
}
#endif

static void check_riff_list(file_recovery_t *fr, const unsigned int depth, const uint64_t start, const uint64_t end)
{
  uint64_t file_size;
  riff_list_header list_header;
  if(depth>5)
    return;
  for(file_size=start; file_size < end;)
  {
    if(fseek(fr->handle, file_size, SEEK_SET)<0)
    {
      fr->offset_error=file_size;
      return;
    }
    if (fread(&list_header, sizeof(list_header), 1, fr->handle)!=1)
    {
      fr->offset_error=file_size;
      return;
    }
    if(memcmp(&list_header.dwList, "LIST", 4) == 0)
    {
#ifdef DEBUG_RIFF
      log_riff_list(file_size, depth, &list_header);
#endif
      check_riff_list(fr, depth+1, file_size + sizeof(list_header), file_size + 8 + le32(list_header.dwSize) - 1);
    }
    else
    {
#ifdef DEBUG_RIFF
      /* It's a chunk */
      log_riff_chunk(file_size, depth, &list_header);
#endif
    }
    file_size += 8 + le32(list_header.dwSize);
    /* align to word boundary */
    file_size += (file_size&1);
  }
}

static void file_check_avi(file_recovery_t *fr)
{
  fr->file_size = 0;
  fr->offset_error=0;
  fr->offset_ok=0;
  while(1)
  {
    const uint64_t file_size=fr->file_size;
    riff_list_header list_header;
    if(fseek(fr->handle, fr->file_size, SEEK_SET)<0)
    {
      fr->file_size=0;
      return ;
    }
    if (fread(&list_header, sizeof(list_header), 1, fr->handle)!=1)
    {
      fr->file_size=0;
      return;
    }
#ifdef DEBUG_RIFF
    log_riff_list(file_size, 0, &list_header);
#endif
    if(memcmp(&list_header.dwList, "RIFF", 4) != 0)
    {
      fr->offset_error=fr->file_size;
      fr->file_size=fr->file_size;
      return;
    }
    check_riff_list(fr, 1, file_size + sizeof(list_header), file_size + 8 + le32(list_header.dwSize) - 1);
    if(fr->offset_error > 0)
    {
      fr->file_size=0;
      return;
    }
    fr->file_size=file_size + 8 + le32(list_header.dwSize);
  }
}

static int data_check_avi(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery)
{
  while(file_recovery->calculated_file_size + buffer_size/2  >= file_recovery->file_size &&
      file_recovery->calculated_file_size + 12 < file_recovery->file_size + buffer_size/2)
  {
    unsigned int i=file_recovery->calculated_file_size - file_recovery->file_size + buffer_size/2;
    const riff_chunk_header *chunk_header=(const riff_chunk_header*)&buffer[i];
    if(memcmp(&buffer[i], "RIFF", 4)==0 && memcmp(&buffer[i+8], "AVIX", 4)==0)
      file_recovery->calculated_file_size += 8 + le32(chunk_header->dwSize);
    else
      return 2;
  }
  return 1;
}

int data_check_avi_stream(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery)
{
  while(file_recovery->calculated_file_size + buffer_size/2  >= file_recovery->file_size &&
      file_recovery->calculated_file_size + 8 < file_recovery->file_size + buffer_size/2)
  {
    unsigned int i=file_recovery->calculated_file_size - file_recovery->file_size + buffer_size/2;
    const riff_chunk_header *chunk_header=(const riff_chunk_header*)&buffer[i];
    if(buffer[i+2]!='d' || buffer[i+3]!='b')	/* Video Data Binary ?*/
      return 2;
    file_recovery->calculated_file_size += 8 + le32(chunk_header->dwSize);
  }
  return 1;
}

static void file_check_size_rifx(file_recovery_t *file_recovery)
{
  if(file_recovery->file_size<file_recovery->calculated_file_size)
    file_recovery->file_size=0;
}

static int header_check_riff(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new)
{
  if(memcmp(buffer,riff_header,sizeof(riff_header))==0)
  {
    reset_file_recovery(file_recovery_new);
    file_recovery_new->file_check=&file_check_size;
    file_recovery_new->data_check=&data_check_size;
    file_recovery_new->calculated_file_size=(uint64_t)buffer[4]+(((uint64_t)buffer[5])<<8)+(((uint64_t)buffer[6])<<16)+(((uint64_t)buffer[7])<<24)+8;
    if(memcmp(&buffer[8],"NUND",4)==0)
    {
      /* Cubase Project File */
      file_recovery_new->extension="cpr";
      file_recovery_new->calculated_file_size=(((uint64_t)buffer[4])<<24) +
	(((uint64_t)buffer[5])<<16) + (((uint64_t)buffer[6])<<8) +
	(uint64_t)buffer[7] + 12;
      return 1;
    }
    if(memcmp(&buffer[8],"AVI ",4)==0)
    {
      const riff_list_header list_movi={
	.dwList=be32(0x4c495354),	/* LIST */
	.dwSize=le32(4),
	.dwFourCC=be32(0x6d6f7669)	/* movi */
      };
      file_recovery_new->extension="avi";
      /* Is it a raw avi stream with Data Binary chunks ? */
      if(file_recovery_new->calculated_file_size + 4 < buffer_size &&
	memcmp(&buffer[file_recovery_new->calculated_file_size - sizeof(list_movi)], &list_movi, sizeof(list_movi)) ==0 &&
	  buffer[file_recovery_new->calculated_file_size+2]=='d' &&
	  buffer[file_recovery_new->calculated_file_size+3]=='b')
      {
	file_recovery_new->data_check=&data_check_avi_stream;
      }
      else
      {
	file_recovery_new->data_check=&data_check_avi;
	file_recovery_new->file_check=&file_check_avi;
      }
      return 1;
    }
    if(memcmp(&buffer[8],"CDDA",4)==0)
      file_recovery_new->extension="cda";
    else if(memcmp(&buffer[8],"CDR",3)==0 || memcmp(&buffer[8],"cdr6",4)==0)
      file_recovery_new->extension="cdr";
    else if(memcmp(&buffer[8],"RMP3",4)==0 || memcmp(&buffer[8],"WAVE",4)==0)
      file_recovery_new->extension="wav";
    /* MIDI sound file */
    else if(memcmp(&buffer[8],"RMID",4)==0)
      file_recovery_new->extension="mid";
    /* MIDI Instruments Definition File */
    else if(memcmp(&buffer[8],"IDF LIST",8)==0)
      file_recovery_new->extension="idf";
    /* Windows Animated Cursor */
    else if(memcmp(&buffer[8],"ACON",4)==0)
      file_recovery_new->extension="ani";
    else
      file_recovery_new->extension="avi";
    return 1;
  }
  return 0;
}

static int header_check_rifx(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new)
{
  if(memcmp(buffer,rifx_header,sizeof(rifx_header))!=0)
    return 0;
  if(memcmp(&buffer[8],"Egg!",4)==0)
  {
    /* After Effects */
    reset_file_recovery(file_recovery_new);
    file_recovery_new->file_check=&file_check_size_rifx;
    file_recovery_new->calculated_file_size=(uint64_t)buffer[7]+(((uint64_t)buffer[6])<<8)+(((uint64_t)buffer[5])<<16)+(((uint64_t)buffer[4])<<24)+8;
    file_recovery_new->extension="aep";
    return 1;
  }
  return 0;
}

static void register_header_check_riff(file_stat_t *file_stat)
{
  register_header_check(0, riff_header,sizeof(riff_header), &header_check_riff, file_stat);
  register_header_check(0, rifx_header,sizeof(rifx_header), &header_check_rifx, file_stat);
}


