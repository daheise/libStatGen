/*
 *  Copyright (C) 2010  Regents of the University of Michigan
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <htslib/sam.h>
#include "GenericSamInterface.h"


GenericSamInterface::GenericSamInterface(const char*const fn, const char* mode, const char*const ref_fn) :
    fp_(sam_open(fn, mode)),
    idx_(nullptr),
    itr_(nullptr),
    hdr_(nullptr),
    rec_(bam_init1()),
    eof_(false)
{
    if (ref_fn)
        hts_set_fai_filename(fp_, ref_fn);
}

GenericSamInterface::GenericSamInterface(const char*const fn, const char* mode, const htsFormat& fmt, const char*const ref_fn) :
  fp_(sam_open_format(fn, mode, &fmt)), // TODO: fmt may need to be copied as a member
  idx_(nullptr),
  itr_(nullptr),
  hdr_(nullptr),
  rec_(bam_init1()),
  eof_(false)
{
    if (ref_fn)
        hts_set_fai_filename(fp_, ref_fn);
}

GenericSamInterface::~GenericSamInterface()
{
    if (hdr_)
        bam_hdr_destroy(hdr_);

    if (rec_)
        bam_destroy1(rec_);

    if (itr_)
        hts_itr_destroy(itr_);

    if (idx_)
        hts_idx_destroy(idx_);

    if (fp_)
        sam_close(fp_);
}

bool GenericSamInterface::loadIndex()
{
    if (idx_)
        return false;
    idx_ = sam_index_load(fp_, fp_->fn);
    return (bool)idx_;
}

bool GenericSamInterface::loadIndex(const char*const idx_name)
{
    if (idx_)
        return false;
    idx_ = sam_index_load2(fp_, fp_->fn, idx_name);
    return (bool)idx_;
}

bool GenericSamInterface::setReadSection(const char*const ref_name, std::int32_t beg, std::int32_t end)
{
    return this->setReadSection(bam_name2id(hdr_, ref_name), beg, end);
}

bool GenericSamInterface::setReadSection(std::int32_t ref_id, std::int32_t beg, std::int32_t end)
{
    if (!idx_)
        return false;
    eof_ = false;
    if (itr_)
        hts_itr_destroy(itr_);
    itr_ = sam_itr_queryi(idx_, ref_id, beg, end);
    return (bool)itr_;
}

// Reads the header section from the specified BAM file and stores it in
// the passed in header.
// Returns false and updates the status on failure.
bool GenericSamInterface::readHeader(SamFileHeader& header, SamStatus& samStatus)
{
    bool ret = false;
    if (!fp_)
    {
      samStatus.setStatus(SamStatus::FAIL_IO, "File not open.");
    }
    else
    {
      if (hdr_)
      {
        samStatus.setStatus(SamStatus::FAIL_ORDER, "Header already exists");
      }
      else
      {
        // Clear the passed in header.
        header.resetHeader();

        hdr_ = sam_hdr_read(fp_);
        if (!hdr_)
        {
          samStatus.setStatus(SamStatus::FAIL_IO, "Error reading header from file");
        }
        else
        {
          std::string tmp(hdr_->text, hdr_->l_text);
          if (!header.addHeader(tmp.c_str()))
            samStatus.setStatus(SamStatus::FAIL_PARSE, header.getErrorMessage());
          else
            ret = true;
        }
      }
    }
    return ret;
}

// Writes the specified header into the specified BAM file.
// Returns false and updates the status on failure.
bool GenericSamInterface::writeHeader(SamFileHeader& header, SamStatus& samStatus)
{
    bool ret = false;

    if (!fp_)
    {
      samStatus.setStatus(SamStatus::FAIL_IO, "File not open.");
    }
    else
    {
        if (hdr_)
        {
            samStatus.setStatus(SamStatus::FAIL_ORDER, "Header already exists");
        }
        else
        {
            std::string tmp;
            header.getHeaderString(tmp);
            hdr_ = sam_hdr_parse(tmp.size(), tmp.data());
            if (!hdr_)
            {
                samStatus.setStatus(SamStatus::FAIL_PARSE, "Header data corrupt");
            }
            else
            {
                //https://github.com/samtools/htslib/issues/104
                hdr_->l_text = tmp.size();
                hdr_->text = (char*)tmp.data();

                if (sam_hdr_write(fp_, hdr_) != 0)
                    samStatus.setStatus(SamStatus::FAIL_IO, "Failed to write header");
                else
                    ret = true;

                hdr_->l_text = 0;
                hdr_->text = nullptr;
            }
        }

    }
    return ret;
}

// Reads the next record from the specified BAM file and stores it in
// the passed in record.
void GenericSamInterface::readRecord(SamFileHeader& header, SamRecord& record, SamStatus& samStatus)
{
    if (fp_)
    {
        if (idx_ && itr_)
        {
            if (sam_itr_next(fp_, itr_, rec_) < 0)
            {
                samStatus.setStatus(SamStatus::NO_MORE_RECS, "End of file");
                eof_ = true;
            }

        }
        else
        {
            if (sam_read1(fp_, hdr_, rec_) < 0)
            {
                samStatus.setStatus(SamStatus::NO_MORE_RECS, "End of file");
                eof_ = true;
            }
        }

        if (!eof_)
            record.setBufferFromHtsRec(rec_, header);
    }
}

// Writes the specified record into the specified BAM file.
SamStatus::Status GenericSamInterface::writeRecord(SamFileHeader& header, SamRecord& record, SamRecord::SequenceTranslation translation)
{
    if (!fp_)
        return SamStatus::FAIL_IO;

    SamStatus::Status ret = record.copyRecordBufferToHts(rec_, translation);
    if (ret == SamStatus::SUCCESS)
    {
        if (sam_write1(fp_, hdr_, rec_) < 0)
        {
            ret = SamStatus::FAIL_IO;
        }
    }
    return ret;
}

bool GenericSamInterface::isEOF()
{
    return (!fp_ || eof_);
}

htsExactFormat GenericSamInterface::format() const
{
  if (fp_)
      return fp_->format.format;
  else
      return htsExactFormat::format_maximum;
}
