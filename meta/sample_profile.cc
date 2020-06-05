/*
Part of meta-scallop
(c) 2020 by Mingfu Shao, The Pennsylvania State University
See LICENSE for licensing.
*/

#include "sample_profile.h"
#include "htslib/bgzf.h"
#include "constants.h"

mutex sample_profile::bam_lock;

sample_profile::sample_profile()
{
	sfn = NULL;
	hdr = NULL;
	bridged_bam = NULL;
	data_type = DEFAULT;
}

int sample_profile::open_align_file()
{
	sfn = sam_open(align_file.c_str(), "r");
	hdr = sam_hdr_read(sfn);
	return 0;
}

int sample_profile::open_bridged_bam(const string &dir)
{
	char file[10240];
	sprintf(file, "%s/%d.bam", dir.c_str(), sample_id);
	bridged_bam = bgzf_open(file, "w");
	bam_hdr_write(bridged_bam, hdr);
	return 0;
}

int sample_profile::close_bridged_bam()
{
	if(bridged_bam == NULL) return 0;
	int f = bgzf_close(bridged_bam);
	printf("close bridged bam for sample %s with code %d\n", align_file.c_str(), f);
	return 0;
}

int sample_profile::close_align_file()
{
    if(hdr != NULL) bam_hdr_destroy(hdr);
    if(sfn != NULL) sam_close(sfn);
	return 0;
}

int sample_profile::build_index_iterators()
{
	hts_idx_t *idx = sam_index_load(sfn, index_file.c_str());

	iters.clear();
	for(int i = 0; i < hdr->n_targets; i++)
	{
		hts_itr_t *iter = sam_itr_querys(idx, hdr, hdr->target_name[i]);
		iters.push_back(iter);
	}

	hts_idx_destroy(idx);
	return 0;
}

int sample_profile::destroy_index_iterators()
{
	for(int i = 0; i < iters.size(); i++)
	{
		hts_itr_destroy(iters[i]);
	}
	return 0;
}

int sample_profile::print()
{
	printf("file = %s, type = %d\n", align_file.c_str(), data_type);
	return 0;
}
