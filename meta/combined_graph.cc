#include "bridge_solver.h"
#include "parameters.h"
#include "combined_graph.h"
#include "config.h"
#include "essential.h"
#include <sstream>
#include <algorithm>

combined_graph::combined_graph()
{
	num_combined = 0;
	strand = '?';
}

int combined_graph::copy_meta_information(const combined_graph &cb)
{
	gid = cb.gid;
	chrm = cb.chrm;
	strand = cb.strand;
	sp = cb.sp;
	return 0;
}

int combined_graph::set_gid(int instance, int subindex)
{
	char name[10240];
	sprintf(name, "instance.%d.%d.0", instance, subindex);
	gid = name;
	return 0;
}

int combined_graph::build(splice_graph &gr, const phase_set &p, const vector<pereads_cluster> &ub)
{
	chrm = gr.chrm;
	strand = gr.strand;
	num_combined = 1;

	build_regions(gr);
	build_start_bounds(gr);
	build_end_bounds(gr);
	build_splices_junctions(gr);
	ps = p;
	vc = ub;
	return 0;
}
	
int combined_graph::build_regions(splice_graph &gr)
{
	regions.clear();
	int n = gr.num_vertices() - 1;
	for(int i = 1; i < n; i++)
	{
		if(gr.degree(i) == 0) continue;
		double w = gr.get_vertex_weight(i);
		vertex_info vi = gr.get_vertex_info(i);
		PI32 p(vi.lpos, vi.rpos);
		regions.push_back(PPDI(p, DI(w, 1)));
	}
	return 0;
}

int combined_graph::build_start_bounds(splice_graph &gr)
{
	sbounds.clear();
	PEEI pei = gr.out_edges(0);
	int n = gr.num_vertices() - 1;
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(s == 0 && t > s);
		if(t == n) continue;
		int32_t p = gr.get_vertex_info(t).lpos;
		double w = gr.get_edge_weight(*it);
		PIDI pi(p, DI(w, 1));
		sbounds.push_back(pi);
	}
	return 0;
}

int combined_graph::build_end_bounds(splice_graph &gr)
{
	tbounds.clear();
	int n = gr.num_vertices() - 1;
	PEEI pei = gr.in_edges(n);
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(t == n);
		assert(s < t);
		if(s == 0) continue;
		int32_t p = gr.get_vertex_info(s).rpos;
		double w = gr.get_edge_weight(*it);
		PIDI pi(p, DI(w, 1));
		tbounds.push_back(pi);
	}
	return 0;
}

int combined_graph::build_splices_junctions(splice_graph &gr)
{
	junctions.clear();
	splices.clear();
	PEEI pei = gr.edges();
	set<int32_t> sp;
	int n = gr.num_vertices() - 1;
	for(edge_iterator it = pei.first; it != pei.second; it++)
	{
		int s = (*it)->source(); 
		int t = (*it)->target();
		assert(s < t);
		if(s == 0) continue;
		if(t == n) continue;
		double w = gr.get_edge_weight(*it);
		int32_t p1 = gr.get_vertex_info(s).rpos;
		int32_t p2 = gr.get_vertex_info(t).lpos;
		int c = 1;
		if(p1 >= p2) continue;

		PI32 p(p1, p2);
		junctions.push_back(PPDI(p, DI(w, 1)));
		sp.insert(p1);
		sp.insert(p2);
	}
	splices.assign(sp.begin(), sp.end());
	sort(splices.begin(), splices.end());
	return 0;
}

int combined_graph::get_overlapped_splice_positions(const vector<int32_t> &v) const
{
	vector<int32_t> vv(v.size(), 0);
	vector<int32_t>::iterator it = set_intersection(v.begin(), v.end(), splices.begin(), splices.end(), vv.begin());
	return it - vv.begin();
}

int combined_graph::combine(combined_graph *cb)
{
	vector<combined_graph*> v;
	v.push_back(cb);
	combine(v);
	return 0;
}

int combined_graph::combine(vector<combined_graph*> &gv)
{
	if(gv.size() == 0) return 0;

	/*
	chrm = gv[0]->chrm;
	strand = gv[0]->strand;
	sp = gv[0]->sp;
	num_combined = 0;
	*/

	split_interval_double_map imap;
	map<PI32, DI> mj;
	map<int32_t, DI> ms;
	map<int32_t, DI> mt;

	combine_regions(imap);
	combine_junctions(mj);
	combine_start_bounds(ms);
	combine_end_bounds(mt);

	for(int i = 0; i < gv.size(); i++)
	{
		combined_graph *gt = gv[i];
		gt->combine_regions(imap);
		gt->combine_junctions(mj);
		gt->combine_start_bounds(ms);
		gt->combine_end_bounds(mt);
		ps.combine(gt->ps);
		num_combined += gt->num_combined;
	}

	regions.clear();
	for(SIMD it = imap.begin(); it != imap.end(); it++)
	{
		int32_t l = lower(it->first);
		int32_t r = upper(it->first);
		regions.push_back(PPDI(PI32(l, r), DI(it->second, 1)));
	}

	junctions.assign(mj.begin(), mj.end());
	sbounds.assign(ms.begin(), ms.end());
	tbounds.assign(mt.begin(), mt.end());

	return 0;
}

int combined_graph::combine_regions(split_interval_double_map &imap) const
{
	for(int i = 0; i < regions.size(); i++)
	{
		PI32 p = regions[i].first;
		double w = regions[i].second.first;
		imap += make_pair(ROI(p.first, p.second), w);
	}
	return 0;
}

int combined_graph::combine_junctions(map<PI32, DI> &m) const
{
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		DI d = junctions[i].second;

		map<PI32, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<PI32, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::combine_start_bounds(map<int32_t, DI> &m) const
{
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		DI d = sbounds[i].second;

		map<int32_t, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<int32_t, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::combine_end_bounds(map<int32_t, DI> &m) const
{
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		DI d = tbounds[i].second;

		map<int32_t, DI>::iterator x = m.find(p);

		if(x == m.end())
		{
			m.insert(pair<int32_t, DI>(p, d));
		}
		else 
		{
			x->second.first += d.first;
			x->second.second += d.second;
		}
	}
	return 0;
}

int combined_graph::append(const pereads_cluster &pc, const bridge_path &bbp)
{
	assert(bbp.type >= 0);
	append_regions(pc, bbp);
	append_junctions(pc, bbp);
	add_phases_from_bridged_pereads_cluster(pc, bbp, ps);
	return 0;
}

int combined_graph::append_regions(const pereads_cluster &pc, const bridge_path &bbp)
{
	int32_t p1, p2;
	if(bbp.chain.size() == 0)
	{
		p1 = pc.extend[1];
		p2 = pc.extend[2];
		if(p1 >= p2) return 0;
		PPDI pi(PI32(p1, p2), DI(pc.count, 1));
		regions.push_back(pi);
		return 0;
	}

	// append first region
	p1 = pc.extend[1];
	p2 = bbp.chain[0];
	if(p1 < p2)
	{
		PPDI pi(PI32(p1, p2), DI(pc.count, 1));
		regions.push_back(pi);
	}
	else
	{
		PPDI pi(PI32(p2, p1), DI(0.1, 1));
		regions.push_back(pi);
	}

	// append middle regions
	for(int i = 0; i < bbp.chain.size() / 2 - 1; i++)
	{
		p1 = bbp.chain[i * 2 + 1];
		p2 = bbp.chain[i * 2 + 2];
		assert(p1 < p2);
		PPDI pi(PI32(p1, p2), DI(pc.count, 1));
		regions.push_back(pi);
	}

	// append last region
	p1 = bbp.chain.back();
	p2 = pc.extend[2];
	if(p1 < p2)
	{
		PPDI pi(PI32(p1, p2), DI(pc.count, 1));
		regions.push_back(pi);
	}
	else
	{
		PPDI pi(PI32(p2, p1), DI(0.1, 1));
		regions.push_back(pi);
	}
	return 0;
}

int combined_graph::append_junctions(const pereads_cluster &pc, const bridge_path &bbp)
{
	// append middle regions
	for(int i = 0; i < bbp.chain.size() / 2; i++)
	{
		int32_t p1 = bbp.chain[i * 2 + 0];
		int32_t p2 = bbp.chain[i * 2 + 1];
		assert(p1 < p2);
		PPDI pi(PI32(p1, p2), DI(pc.count, 1));
		junctions.push_back(pi);
	}
	return 0;
}

int combined_graph::build_splice_graph(splice_graph &gr)
{
	gr.clear();

	gr.gid = gid;
	gr.chrm = chrm;
	gr.strand = strand;

	// add vertices
	gr.add_vertex();	// 0
	PIDI sb = get_leftmost_bound();
	vertex_info v0;
	v0.lpos = sb.first;
	v0.rpos = sb.first;
	gr.set_vertex_weight(0, 0);
	gr.set_vertex_info(0, v0);

	for(int i = 0; i < regions.size(); i++) 
	{
		gr.add_vertex();
		vertex_info vi;
		vi.lpos = regions[i].first.first;
		vi.rpos = regions[i].first.second;
		vi.count = regions[i].second.second;
		double w = regions[i].second.first;
		vi.length = vi.rpos - vi.lpos;
		gr.set_vertex_weight(i + 1, w);
		gr.set_vertex_info(i + 1, vi);
	}

	gr.add_vertex();	// n
	PIDI tb = get_rightmost_bound();
	vertex_info vn;
	vn.lpos = tb.first;
	vn.rpos = tb.first;
	gr.set_vertex_info(regions.size() + 1, vn);
	gr.set_vertex_weight(regions.size() + 1, 0);

	// build vertex index
	gr.build_vertex_index();

	// add sbounds
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		double w = sbounds[i].second.first;
		int c = sbounds[i].second.second;

		assert(gr.lindex.find(p) != gr.lindex.end());
		int k = gr.lindex[p];
		edge_descriptor e = gr.add_edge(0, k);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// add tbounds
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		double w = tbounds[i].second.first;
		int c = tbounds[i].second.second;

		assert(gr.rindex.find(p) != gr.rindex.end());
		int k = gr.rindex[p];
		edge_descriptor e = gr.add_edge(k, gr.num_vertices() - 1);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// add junctions
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		double w = junctions[i].second.first;
		int c = junctions[i].second.second;

		// TODO, should be asserted
		if(gr.rindex.find(p.first) == gr.rindex.end()) continue;
		if(gr.lindex.find(p.second) == gr.lindex.end()) continue;
		int s = gr.rindex[p.first];
		int t = gr.lindex[p.second];
		edge_descriptor e = gr.add_edge(s, t);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}

	// connect adjacent regions
	for(int i = 1; i < regions.size(); i++)
	{
		int32_t p1 = regions[i - 1].first.second;
		int32_t p2 = regions[i - 0].first.first;

		assert(p1 <= p2);
		if(p1 < p2) continue;

		PPDI ss = regions[i - 1];
		PPDI tt = regions[i - 0];
		if(ss.first.second != tt.first.first) continue;

		// TODO
		/*
		double w1 = gr.get_out_weights(i + 0);
		double w2 = gr.get_in_weights(i + 1);
		double ws = ss.second.first - w1;
		double wt = tt.second.first - w2;
		double w = (ws + wt) * 0.5;
		*/

		int xd = gr.out_degree(i + 0);
		int yd = gr.in_degree(i + 1);
		double w = (xd < yd) ? ss.second.first : tt.second.first;
		int c = ss.second.second;
		if(ss.second.second > tt.second.second) c = tt.second.second;

		if(w < 1) w = 1;
		edge_descriptor e = gr.add_edge(i + 0, i + 1);
		edge_info ei;
		ei.weight = w;
		ei.count = c;
		gr.set_edge_info(e, ei);
		gr.set_edge_weight(e, w);
	}
	return 0;
}

set<int32_t> combined_graph::get_reliable_splices(int samples, double weight)
{
	map<int32_t, DI> m;
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		int32_t p1 = p.first;
		int32_t p2 = p.second;
		double w = junctions[i].second.first;
		int c = junctions[i].second.second;

		if(m.find(p1) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(p1, di));
		}
		else
		{
			m[p1].first += w;
			m[p1].second += c;
		}

		if(m.find(p2) == m.end())
		{
			DI di(w, c);
			m.insert(pair<int32_t, DI>(p2, di));
		}
		else
		{
			m[p2].first += w;
			m[p2].second += c;
		}
	}

	set<int32_t> s;
	for(map<int32_t, DI>::iterator it = m.begin(); it != m.end(); it++)
	{
		int32_t p = it->first;
		double w = it->second.first;
		int c = it->second.second;
		if(w < weight && c < samples) continue;
		s.insert(p);
	}
	return s;
}

int combined_graph::clear()
{
	num_combined = 0;
	gid = "";
	chrm = "";
	strand = '.';
	splices.clear();
	regions.clear();
	junctions.clear();
	sbounds.clear();
	tbounds.clear();
	ps.clear();
	vc.clear();
	return 0;
}

int combined_graph::print(int index)
{
	int pereads = 0;
	for(int i = 0; i < vc.size(); i++)
	{
		pereads += vc[i].chain1.size();
		pereads += vc[i].chain2.size();
		pereads += vc[i].bounds.size();
		pereads += vc[i].extend.size();
	}

	printf("combined-graph %d: gid = %s, #combined = %d, chrm = %s, strand = %c, #regions = %lu, #sbounds = %lu, #tbounds = %lu, #junctions = %lu, #phases = %lu, #pereads = %lu / %d\n", 
			index, gid.c_str(), num_combined, chrm.c_str(), strand, regions.size(), sbounds.size(), tbounds.size(), junctions.size(), ps.pmap.size(), vc.size(), pereads);

	return 0;
	for(int i = 0; i < regions.size(); i++)
	{
		PI32 p = regions[i].first;
		DI d = regions[i].second;
		printf("region %d: [%d, %d), w = %.2lf, c = %d\n", i, p.first, p.second, d.first, d.second);
	}
	for(int i = 0; i < junctions.size(); i++)
	{
		PI32 p = junctions[i].first;
		DI d = junctions[i].second;
		printf("junction %d: [%d, %d), w = %.2lf, c = %d\n", i, p.first, p.second, d.first, d.second);
	}
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		DI d = sbounds[i].second;
		printf("sbound %d: %d, w = %.2lf, c = %d\n", i, p, d.first, d.second);
	}
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		DI d = tbounds[i].second;
		printf("tbound %d: %d, w = %.2lf, c = %d\n", i, p, d.first, d.second);
	}
	ps.print();
	return 0;
}

PIDI combined_graph::get_leftmost_bound()
{
	PIDI x;
	x.first = -1;
	for(int i = 0; i < sbounds.size(); i++)
	{
		int32_t p = sbounds[i].first;
		if(x.first == -1 || p < x.first)
		{
			x = sbounds[i];
		}
	}
	return x;
}

PIDI combined_graph::get_rightmost_bound()
{
	PIDI x;
	x.first = -1;
	for(int i = 0; i < tbounds.size(); i++)
	{
		int32_t p = tbounds[i].first;
		if(x.first == -1 || p > x.first)
		{
			x = tbounds[i];
		}
	}
	return x;
}
