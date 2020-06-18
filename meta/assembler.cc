/*
Part of meta-scallop
(c) 2020 by Mingfu Shao, The Pennsylvania State University
See LICENSE for licensing.
*/

#include "assembler.h"
#include "scallop.h"
#include "graph_reviser.h"
#include "bridge_solver.h"
#include "essential.h"
#include "constants.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <ctime>
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>
#include <boost/pending/disjoint_sets.hpp>

assembler::assembler(const parameters &p)
	: cfg(p)
{
}

int assembler::assemble(vector<combined_graph*> gv, int batch, int instance, transcript_set &ts, vector<sample_profile> &samples)
{
	int subindex = 0;

	if(gv.size() == 1)
	{
		combined_graph &gt = *(gv[0]);
		gt.set_gid(batch, instance, subindex++);
		gt.refine_junctions();
		assemble(gt, ts, TRANSCRIPT_COUNT_ADD_COVERAGE_ADD);
		ts.increase_count(1);
		if(cfg.output_bridged_bam_dir != "" && gt.vc.size() >= 1)
		{
			sample_profile &sp = samples[gt.sid];
			sp.bam_lock.lock();
			sp.open_bridged_bam(cfg.output_bridged_bam_dir);
			for(int k = 0; k < gt.vc.size(); k++)
			{
				write_unbridged_pereads_cluster(sp.bridged_bam, gt.vc[k]);
			}
			sp.close_bridged_bam();
			sp.bam_lock.unlock();
		}
	}
	else
	{
		combined_graph cx(cfg);
		resolve_cluster(gv, cx, samples);

		for(int i = 0; i < gv.size(); i++)
		{
			gv[i]->set_gid(batch, instance, subindex++);
			assemble(*gv[i], ts, TRANSCRIPT_COUNT_ADD_COVERAGE_ADD);
		}

		cx.set_gid(batch, instance, subindex++);
		assemble(cx, ts, TRANSCRIPT_COUNT_ADD_COVERAGE_NUL);
	}

	return 0;
}

int assembler::assemble(combined_graph &cb, transcript_set &ts, int mode)
{
	vector<transcript> vt;
	assemble(cb, vt);

	for(int k = 0; k < vt.size(); k++)
	{
		ts.add(vt[k], 1, cb.sid, mode);
	}
	return 0;
}

int assembler::assemble(combined_graph &cb, vector<transcript> &vt)
{
	// rebuild splice graph
	splice_graph gx;
	cb.build_splice_graph(gx, cfg);
	gx.gid = cb.gid;
	assemble(gx, cb.ps, vt, cb.num_combined);
	return 0;
}

int assembler::assemble(splice_graph &gx, phase_set &px, vector<transcript> &vt, int combined)
{
	gx.build_vertex_index();
	gx.extend_strands();

	map<int32_t, int32_t> smap, tmap;
	group_start_boundaries(gx, smap, cfg.max_group_boundary_distance);
	group_end_boundaries(gx, tmap, cfg.max_group_boundary_distance);
	px.project_boundaries(smap, tmap);

	refine_splice_graph(gx);
	hyper_set hx(gx, px);
	hx.filter_nodes(gx);

	/*
	gx.print();
	if(gx.num_vertices() <= 40) 
	{
		string texfile = "tex/" + gx.gid + ".tex";
		gx.draw(texfile);
	}
	*/

	scallop sx(gx, hx, cfg);
	sx.assemble();

	int z = 0;
	for(int k = 0; k < sx.trsts.size(); k++)
	{
		transcript &t = sx.trsts[k];
		z++;
		t.RPKM = 0;
		vt.push_back(t);
	}

	//printf("assemble %s: %d transcripts, combined = %d, graph with %lu vertices and %lu edges, phases = %lu\n", gx.gid.c_str(), z, combined, gx.num_vertices(), gx.num_edges(), px.pmap.size());

	return 0;
}

int assembler::resolve_cluster(vector<combined_graph*> gv, combined_graph &cb, vector<sample_profile> &samples)
{
	assert(gv.size() >= 2);

	// construct combined graph
	cb.copy_meta_information(*(gv[0]));
	cb.combine(gv);
	cb.sid = -1;

	cb.refine_junctions(gv, samples);

	// rebuild splice graph
	splice_graph gx;
	cb.build_splice_graph(gx, cfg);
	gx.build_vertex_index();

	// collect and bridge all unbridged pairs
	vector<pereads_cluster> vc;
	vector<PI> index(gv.size());
	int length_low = 999;
	int length_high = 0;
	for(int k = 0; k < gv.size(); k++)
	{
		combined_graph &gt = *(gv[k]);
		assert(gt.sid >= 0 && gt.sid < samples.size());
		//gt.project_junctions(jm);
		sample_profile &sp = samples[gt.sid];
		if(sp.insertsize_low < length_low) length_low = sp.insertsize_low;
		if(sp.insertsize_high > length_high) length_high = sp.insertsize_high;
		index[k].first = vc.size();
		vc.insert(vc.end(), gt.vc.begin(), gt.vc.end());
		index[k].second = vc.size();
	}

	bridge_solver br(gx, vc, cfg, length_low, length_high);
	br.build_phase_set(cb.ps);

	//printf("cluster-bridge, combined = %lu, ", gv.size()); br.print();

	// resolve individual graphs
	for(int i = 0; i < gv.size(); i++)
	{
		combined_graph g1(cfg);
		for(int k = index[i].first; k < index[i].second; k++)
		{
			if(br.opt[k].type < 0) continue;
			g1.append(vc[k], br.opt[k]);
		}
		gv[i]->combine(&g1);
	}

	// write bridged and unbridged reads
	if(cfg.output_bridged_bam_dir != "")
	{
		for(int i = 0; i < gv.size(); i++)
		{
			combined_graph &gt = *(gv[i]);
			sample_profile &sp = samples[gt.sid];
			sp.bam_lock.lock();
			sp.open_bridged_bam(cfg.output_bridged_bam_dir);
			for(int k = index[i].first; k < index[i].second; k++)
			{
				//vc[k].print(k);
				//br.opt[k].print(k);
				if(br.opt[k].type < 0) 
				{
					write_unbridged_pereads_cluster(sp.bridged_bam, vc[k]);
				}
				else
				{
					write_bridged_pereads_cluster(sp.bridged_bam, vc[k], br.opt[k].whole);
				}
			}
			sp.close_bridged_bam();
			sp.bam_lock.unlock();
		}
	}

	// clear to release memory
	for(int i = 0; i < gv.size(); i++) gv[i]->vc.clear();

	return 0;
}
