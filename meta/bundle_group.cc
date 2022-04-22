/*
Part of aletsch
(c) 2020 by Mingfu Shao, The Pennsylvania State University
See LICENSE for licensing.
*/

#include <algorithm>
#include <sstream>
#include <fstream>
#include "bundle_group.h"
#include "parameters.h"
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

mutex bundle_group::gmutex;

bundle_group::bundle_group(string c, char s, const parameters &f)
	: cfg(f)
{
	chrm = c;
	strand = s;
}

int bundle_group::resolve()
{
	grouped.assign(gset.size(), false);

	build_splices();
	build_splice_index();

	// round one
	min_similarity = cfg.max_grouping_similarity;
	min_group_size = cfg.max_group_size;
	boost::asio::thread_pool pool1(cfg.max_threads);
	for(auto &z: sindex)
	{
		const set<int> &s = z.second;
		boost::asio::post(pool1, [this, &s]{ this->process_subset1(s); });
	}
	pool1.join();
	stats(1);

	// round two
	disjoint_set ds(gset.size());
	min_similarity = cfg.min_grouping_similarity;
	min_group_size = 1;
	boost::asio::thread_pool pool2(cfg.max_threads);
	for(auto &z: sindex)
	{
		const set<int> &s = z.second;
		boost::asio::post(pool2, [this, &s, &ds]{ this->process_subset2(s, ds); });
	}
	pool2.join();
	build_groups(ds);
	stats(2);

	sindex.clear();
	return 0;
}

int bundle_group::process_subset1(const set<int> &s)
{
	gmutex.lock();
	vector<int> ss = filter(s);
	gmutex.unlock();

	vector<PPID> vpid;
	build_similarity(ss, vpid, true);

	gmutex.lock();
	vector<PPID> v = filter(ss, vpid);
	disjoint_set ds(ss.size());
	augment_disjoint_set(v, ds);
	build_groups(ss, ds);
	gmutex.unlock();

	return 0;
}

int bundle_group::process_subset2(const set<int> &s, disjoint_set &ds)
{
	vector<int> ss = filter(s);

	vector<PPID> vpid;
	build_similarity(ss, vpid, false);

	vector<PPID> v = filter(vpid);

	gmutex.lock();
	augment_disjoint_set(v, ds);
	gmutex.unlock();

	return 0;
}

int bundle_group::build_splices()
{
	splices.clear();
	for(int i = 0; i < gset.size(); i++)
	{
		vector<int32_t> v = gset[i].hcst.get_splices();
		splices.push_back(std::move(v));
	}
	return 0;
}

int bundle_group::build_splice_index()
{
	sindex.clear();
	for(int k = 0; k < gset.size(); k++)
	{
		for(int i = 0; i < splices[k].size(); i++)
		{
			int32_t p = splices[k][i];
			MISI::iterator it = sindex.find(p);
			if(it == sindex.end())
			{
				set<int> s;
				s.insert(k);
				sindex.insert(PISI(p, s));
			}
			else
			{
				it->second.insert(k);
			}
		}
	}
	return 0;
}

int bundle_group::build_similarity(const vector<int> &ss, vector<PPID> &vpid, bool local)
{
	//printf("START BUILD SIMILARITY; cfg.max_num_junctions_to_combine = %d, ss.size() =%lu\n", cfg.max_num_junctions_to_combine, ss.size());
	for(int xi = 0; xi < ss.size(); xi++)
	{
		int i = ss[xi];
		if(splices[i].size() / 2.0 > cfg.max_num_junctions_to_combine) continue;
		for(int xj = 0; xj < ss.size(); xj++)
		{
			int j = ss[xj];
			if(i >= j) continue;
			if(splices[j].size() / 2.0 > cfg.max_num_junctions_to_combine) continue;

			assert(gset[i].chrm == gset[j].chrm);
			assert(gset[i].strand == gset[j].strand);

			vector<int32_t> vv(splices[i].size() + splices[j].size(), 0);
			vector<int32_t>::iterator it = set_intersection(splices[i].begin(), splices[i].end(), splices[j].begin(), splices[j].end(), vv.begin());
			int c = it - vv.begin();
			//double r = c * 1.0 / (splices[i].size() + splices[j].size() - c);
			int small = splices[i].size() < splices[j].size() ? splices[i].size() : splices[j].size();
			double r = c * 1.0 / small;

			printf("graph-similarity: r = %.3lf, c = %d, size1 = %lu, size2 = %lu, sp1 = %d-%d, sp2 = %d-%d\n", 
					r, c, splices[i].size(), splices[j].size(), splices[i].front(), splices[i].back(), splices[j].front(), splices[j].back());

			if(c <= 1.50) continue;
			if(r < min_similarity) continue;

			if(local == true) vpid.push_back(PPID(PI(xi, xj), r));
			else vpid.push_back(PPID(PI(i, j), r));
		}
	}

	sort(vpid.begin(), vpid.end(), [](const PPID &x, const PPID &y){ return x.second > y.second; });

	return 0;
}

int bundle_group::augment_disjoint_set(const vector<PPID> &vpid, disjoint_set &ds)
{
	for(int i = 0; i < vpid.size(); i++)
	{
		int x = vpid[i].first.first;
		int y = vpid[i].first.second;
		int px = ds.find_set(x);
		int py = ds.find_set(y);
		if(px == py) continue;

		int sx = ds.get_size(px);
		int sy = ds.get_size(py);
		if(sx >= cfg.max_group_size) continue;
		if(sy >= cfg.max_group_size) continue;

		ds.link(px, py);

		int q = ds.find_set(px);
		assert(q == ds.find_set(py));
		ds.set_size(q, sx + sy);
	}
	return 0;
}

int bundle_group::build_groups(disjoint_set &ds)
{
	vector<int> ss(gset.size());
	for(int i = 0; i < ss.size(); i++) ss[i] = i;
	build_groups(ss, ds);
	return 0;
}

int bundle_group::build_groups(const vector<int> &ss, disjoint_set &ds)
{
	map<int, int> mm;
	for(int i = 0; i < ss.size(); i++)
	{
		int p = ds.find_set(i);
		int s = ds.get_size(p);
		if(s < min_group_size) continue;
		if(grouped[ss[i]] == true) continue;

		grouped[ss[i]] = true;
		if(mm.find(p) == mm.end())
		{
			vector<int> gv;
			gv.push_back(ss[i]);
			mm.insert(pair<int, int>(p, gvv.size()));
			gvv.push_back(gv);
		}
		else
		{
			int k = mm[p];
			gvv[k].push_back(ss[i]);
		}
	}
	return 0;
}

vector<PPID> bundle_group::filter(const vector<int> &ss, const vector<PPID> &vpid)
{
	vector<PPID> v;
	for(int i = 0; i < vpid.size(); i++)
	{
		int x = vpid[i].first.first;
		int y = vpid[i].first.second;

		if(grouped[ss[x]] == true) continue;
		if(grouped[ss[y]] == true) continue;
		v.push_back(vpid[i]);
	}
	return v;
}

vector<PPID> bundle_group::filter(const vector<PPID> &vpid)
{
	vector<int> ss(gset.size());
	for(int i = 0; i < ss.size(); i++) ss[i] = i;
	return filter(ss, vpid);
}

vector<int> bundle_group::filter(const set<int> &s)
{
	vector<int> ss;
	for(auto &z: s)
	{
		if(grouped[z] == true) continue;
		ss.push_back(z);
	}
	return ss;
}

int bundle_group::stats(int r)
{
	map<int, int> m;
	for(int k = 0; k < gvv.size(); k++)
	{
		int n = gvv[k].size();
		if(m.find(n) == m.end()) m.insert(pair<int, int>(n, 1));
		else m[n]++;
	}

	for(map<int, int>::iterator it = m.begin(); it != m.end(); it++)
	{
		printf("round %d: chrm %s, strand %c, total %d graphs with combined %d graphs\n", r, chrm.c_str(), strand, it->second, it->first);
	}
	return 0;
}

int bundle_group::print()
{
	for(int k = 0; k < gvv.size(); k++)
	{
		printf("combined-graph with %lu children: ", gvv[k].size());
		for(int i = 0; i < gvv[k].size(); i++)
		{
			int g = gvv[k][i];
			printf("(%d, %s), ", g, gset[g].gid.c_str());
		}
		printf("\n");
	}
	printf("\n");
	return 0;
}
