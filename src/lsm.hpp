//
//  lsm.hpp
//  lsm-tree
//
//  Created by Aron Szanto on 3/3/17.
//  Copyright © 2017 Aron Szanto. All rights reserved.
//

#ifndef LSM_H
#define LSM_H

#include "run.hpp"
#include "skipList.hpp"
#include "bloom.hpp"
#include "diskLevel.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>



template <class K, class V>
class LSM {
    
    typedef SkipList<K,V> RunType;
    
    
    
public:
    vector<Run<K,V> *> C_0;
    
    vector<BloomFilter<K> *> filters;
    vector<DiskLevel<K,V> *> diskLevels;
    
    LSM<K,V>(unsigned long initialSize, unsigned int numRuns, double merged_frac, double bf_fp, unsigned int pageSize, unsigned int diskRunsPerLevel):_initialSize(initialSize), _num_runs(numRuns), _frac_runs_merged(merged_frac), _diskRunsPerLevel(diskRunsPerLevel), _num_to_merge(ceil(_frac_runs_merged * _num_runs)), _pageSize(pageSize){
        _activeRun = 0;
        _eltsPerRun = initialSize / numRuns;
        _bfFalsePositiveRate = bf_fp;
        
        
        DiskLevel<K,V> * diskLevel = new DiskLevel<K, V>(pageSize, 1, _num_to_merge * _eltsPerRun, _diskRunsPerLevel, ceil(_diskRunsPerLevel * _frac_runs_merged));

        diskLevels.push_back(diskLevel);
        _numDiskLevels = 1;
        
        
        for (int i = 0; i < _num_runs; i++){
            RunType * run = new RunType(INT32_MIN,INT32_MAX);
            run->set_size(_eltsPerRun);
            C_0.push_back(run);
            
            BloomFilter<K> * bf = new BloomFilter<K>(_eltsPerRun, _bfFalsePositiveRate);
            filters.push_back(bf);
        }
    }
    
    void insert_key(K key, V value) {
//        cout << "inserting key " << key << endl;
        if (C_0[_activeRun]->num_elements() >= _eltsPerRun){
//            cout << "run " << _activeRun << " full, moving to next" << endl;
            ++_activeRun;
        }
        
        if (_activeRun >= _num_runs){
//            cout << "need to merge" << endl;
            do_merge();
        }
        
//        cout << "inserting key " << key << " to run " << _activeRun << endl;
        C_0[_activeRun]->insert_key(key,value);
        filters[_activeRun]->add(&key, sizeof(K));
    }
    
    V lookup(K key){
        bool found = false;
        // TODO keep track of min/max in runs?
//        cout << "looking for key " << key << endl;
        for (int i = _activeRun; i >= 0; --i){
//            cout << "... in run/filter " << i << endl;
            if (!filters[i]->mayContain(&key, sizeof(K)))
                continue;
            
            V lookupRes = C_0[i]->lookup(key, &found);
            if (found)
                return lookupRes;
        }
        // it's not in C_0 so let's look at disk.
        for (int i = _numDiskLevels - 1; i >= 0; --i){
            
            V lookupRes = diskLevels[i]->lookup(key, &found);
            if (found)
                return lookupRes;
        }

        return NULL;
    }
    
    
    unsigned long long num_elements(){
        unsigned long long total = 0;
        for (int i = 0; i <= _activeRun; ++i)
            total += C_0[i]->num_elements();
        return total;
    }
    
    void printElts(){
        cout << "MEMORY BUFFER" << endl;
        for (int i = 0; i <= _activeRun; i++){
            cout << "MEMORY BUFFER RUN " << i << endl;
            auto all = C_0[i]->get_all();
            for (KVPair<K, V> &c : all) {
                cout << c.key << " ";
            }
            cout << endl;

        }
        
        cout << "\nDISK BUFFER" << endl;
        for (int i = 0; i < _numDiskLevels; i++){
            cout << "DISK LEVEL " << i << endl;
            for (int j = 0; j < _diskRunsPerLevel; j++){
                cout << "RUN " << j << endl;
                for (int k = 0; k < diskLevels[i]->_runSize; k++){
                    cout << diskLevels[i]->runs[j]->map[k].key << " ";
                }
                cout << endl;
            }
            cout << endl;
        }
    }
    
//private: // TODO MAKE PRIVATE
    unsigned long _initialSize;
    unsigned int _activeRun;
    unsigned long _eltsPerRun;
    double _bfFalsePositiveRate;
    unsigned int _num_runs;
    double _frac_runs_merged;
    unsigned int _numDiskLevels;
    unsigned int _diskRunsPerLevel;
    unsigned int _num_to_merge;
    unsigned int _pageSize;
    
    void mergeRunsToLevel(int level) {
//        cout << "loc 3" << endl;
//        diskLevels[0]->runs[0]->printElts();
        if (level == _numDiskLevels){ // if this is the last level
//            cout << "loc 4" << endl;
//            diskLevels[0]->runs[0]->printElts();
            cout << "adding a new level: " << level << endl;
            DiskLevel<K,V> * newLevel = new DiskLevel<K, V>(_pageSize, level + 1, diskLevels[level - 1]->_runSize * diskLevels[level - 1]->_mergeSize, _diskRunsPerLevel, ceil(_diskRunsPerLevel * _frac_runs_merged));
//            cout << "loc 5" << endl;
//            diskLevels[0]->runs[0]->printElts();
            diskLevels.push_back(newLevel);
            _numDiskLevels++;
//            cout << "loc 6" << endl;
//            diskLevels[0]->runs[0]->printElts();

        }
        
        if (diskLevels[level]->levelFull()) {
            cout << "level " << level << " full, cascading" << endl;
            mergeRunsToLevel(level + 1); // merge down one, recursively
        }
        

        cout << "writing values from level " << (level - 1) << " to level " << level << endl;
        vector<DiskRun<K, V> *> runsToMerge = diskLevels[level - 1]->getRunsToMerge();
        unsigned long runLen = diskLevels[level - 1]->_runSize;
        cout << "values to write from level " << level - 1 << ": " << endl;
        for (int i = 0; i < runsToMerge.size(); i++){
            for (int j = 0; j < diskLevels[level - 1]->_runSize; j++){
                cout << runsToMerge[i]->map[j].key << " ";
            }
            cout << endl;
        }
        diskLevels[level]->addRuns(runsToMerge, runLen);
        cout << "loc 12" << endl;
        diskLevels[0]->runs[0]->printElts();
        diskLevels[0]->runs[1]->printElts();
        diskLevels[1]->runs[0]->printElts();
        diskLevels[1]->runs[1]->printElts();
        diskLevels[level - 1]->freeMergedRuns(runsToMerge);
        cout << "loc 14" << endl;
        diskLevels[0]->runs[0]->printElts();
        diskLevels[0]->runs[1]->printElts();
        diskLevels[1]->runs[0]->printElts();
        diskLevels[1]->runs[1]->printElts();

        
    }
    
    void do_merge(){
        
        if (_num_to_merge == 0)
            return;
//        cout << "going to merge " << num_to_merge << " runs" << endl;
        vector<KVPair<K, V>> to_merge = vector<KVPair<K,V>>();
        to_merge.reserve(_eltsPerRun * _num_to_merge);
        for (int i = 0; i < _num_to_merge; i++){
//            cout << "grabbing values in and deleting run " << i << endl;
            auto all = C_0[i]->get_all();
//            cout << "values in run " << i << endl;
            
            to_merge.insert(to_merge.begin(), all.begin(), all.end());
            delete C_0[i];
            delete filters[i];
        }
        sort(to_merge.begin(), to_merge.end());
//        cout << "merging to disk" << endl;
        
        if (diskLevels[0]->levelFull()){
            mergeRunsToLevel(1);
            diskLevels[0]->runs[0]->printElts();
            diskLevels[0]->runs[1]->printElts();
            diskLevels[1]->runs[0]->printElts();
            diskLevels[1]->runs[1]->printElts();
        }
        
        
        if (_numDiskLevels == 2){
            cout << "loc 10" << endl;
        
        diskLevels[0]->runs[0]->printElts();
        diskLevels[0]->runs[1]->printElts();
        diskLevels[1]->runs[0]->printElts();
        diskLevels[1]->runs[1]->printElts();
        }
        
        diskLevels[0]->addRunByArray(&to_merge[0], to_merge.size());
    
        if (_numDiskLevels == 2){
            cout << "loc 11" << endl;

        diskLevels[0]->runs[0]->printElts();
        diskLevels[0]->runs[1]->printElts();
        diskLevels[1]->runs[0]->printElts();
        diskLevels[1]->runs[1]->printElts();
        }
    
        C_0.erase(C_0.begin(), C_0.begin() + _num_to_merge);
        filters.erase(filters.begin(), filters.begin() + _num_to_merge);

        _activeRun -= _num_to_merge;
        for (int i = _activeRun; i < _num_runs; i++){
            RunType * run = new RunType(INT32_MIN,INT32_MAX);
            run->set_size(_eltsPerRun);
            C_0.push_back(run);
            
            BloomFilter<K> * bf = new BloomFilter<K>(_eltsPerRun, _bfFalsePositiveRate);
            filters.push_back(bf);
        }
        cout << "finished merging- report: " << endl;
//        printElts();
        

    }
    
};




#endif /* lsm_h */

