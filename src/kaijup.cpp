/* This file is part of Kaiju, Copyright 2015 Peter Menzel and Anders Krogh,
 * Kaiju is licensed under the GPLv3, see the file LICENSE. */

#include <stdint.h>
#include <getopt.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <time.h>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <deque>

#include "./ProducerConsumerQueue/src/ProducerConsumerQueue.hpp"
#include "ReadItem.hpp"
#include "ConsumerThreadp.hpp"
#include "Config.hpp"

extern "C" {
#include "./bwt/bwt.h"
}


void usage(char *progname);
void strip(string &s);
bool isalpha(char & c);
string getCurrentTime();

using namespace std;

int main(int argc, char** argv) {


	Config * config = new Config();

	string fmi_filename = "";
	string in1_filename = "";
	string in2_filename = "";
	string output_filename;

	Mode mode = MEM;

	int min_score = 65;
	int min_fragment_length = 11;
	int seed_length = 7;
	int mismatches  = 0;

	int num_threads = 1;
	bool verbose = false;
	bool debug = false;
	bool paired  = false;

	// --------------------- START ------------------------------------------------------------------
	// Read command line params
	int c;
	while ((c = getopt (argc, argv, "a:hdrvn:m:e:l:f:b:i:j:s:z:o:")) != -1) {
		switch (c)  {
			case 'a': {
									if("mem" == string(optarg)) mode = MEM;					
									else if("greedyblosum" == string(optarg)) mode = GREEDYBLOSUM;					
									else if("greedy" == string(optarg)) mode = GREEDYBLOSUM;					
									else { cerr << "-a must be a valid mode.\n"; usage(argv[0]); }
									break;
								}
			case 'h':
				usage(argv[0]);
			case 'd':
				debug = true; break;
			case 'v':
				verbose = true; break;
			case 'o':
				output_filename = optarg; break;
			case 'f':
				fmi_filename = optarg; break;
			case 'i':
				in1_filename = optarg; break;
			case 'j':
				in2_filename = optarg; break;
			case 'l': {
									try {
										seed_length = stoi(optarg); 
									}
									catch(const std::invalid_argument& ia) {
										cerr << "Invalid argument in -l " << optarg << endl;
									}
									catch (const std::out_of_range& oor) {
										cerr << "Invalid argument in -l " << optarg << endl;
									}
									break;
								}
			case 's': {
									try {
										min_score = stoi(optarg); 
									}
									catch(const std::invalid_argument& ia) {
										cerr << "Invalid argument in -s " << optarg << endl;
									}
									catch (const std::out_of_range& oor) {
										cerr << "Invalid argument in -s " << optarg << endl;
									}
									break;
								}
			case 'm': {
									try {
										min_fragment_length = stoi(optarg); 
									}
									catch(const std::invalid_argument& ia) {
										cerr << "Invalid argument in -m " << optarg << endl;
									}
									catch (const std::out_of_range& oor) {
										cerr << "Invalid argument in -m " << optarg << endl;
									}
									break;
								}
			case 'e': {
									try {
										mismatches = stoi(optarg); 
									}
									catch(const std::invalid_argument& ia) {
										cerr << "Invalid numerical argument in -e " << optarg << endl;
									}
									catch (const std::out_of_range& oor) {
										cerr << "Invalid numerical argument in -e " << optarg << endl;
									}
									break;
								}
			case 'z': {
									try {
										num_threads = stoi(optarg);
									}
									catch(const std::invalid_argument& ia) {
										cerr << "Invalid argument in -z " << optarg << endl;
									}
									catch (const std::out_of_range& oor) {
										cerr << "Invalid argument in -z " << optarg << endl;
									}
									break;
								}
			default:
								usage(argv[0]);
		}
	}
	if(min_score <= 0) { cerr << "Error: Min Score (-s) must be greater than 0."  << endl; usage(argv[0]); }
	if(num_threads <= 0) { cerr << "Error: Number of threads (-z) must be greater than 0."  << endl; usage(argv[0]); }
	if(min_fragment_length <= 0) { cerr << "Error: Min fragment length (-m) must be greater than 0."  << endl; usage(argv[0]); }
	if(mismatches < 0) { cerr << "Error: Number of mismatches must be >= 0."  << endl; usage(argv[0]); }
	if(seed_length < 7) { cerr << "Error: Seed length must be >= 7."  << endl; usage(argv[0]); }
	if(fmi_filename.length() == 0) { cerr << "Error: Please specify the location of the FMI file, using the -f option."  << endl; usage(argv[0]); }
	if(in1_filename.length() == 0) { cerr << "Error: Please specify the location of the input file, using the -i option."  << endl; usage(argv[0]); }
	
	if(debug) {
		cerr << "Parameters: \n";
		cerr << "  minimum fragment length for matches: " << min_fragment_length << "\n";
		cerr << "  minimum blosum score for matches: " << min_score << "\n";
		cerr << "  max number of mismatches within a match: "  << mismatches << "\n";
		cerr << "  run mode: "  << mode << "\n";
		cerr << "  input file 1: " << in1_filename << "\n";
		if(in2_filename.length() > 0)
			cerr << "  input file 2: " << in2_filename << "\n";
	}

	config->mode = mode;
	config->debug = debug;
	config->verbose = verbose;
	config->min_score = min_score;
	config->min_fragment_length = min_fragment_length;
	config->seed_length = seed_length;
	config->mismatches = mismatches;

	if(verbose) cerr << getCurrentTime() << " Reading database" << endl;

	if(verbose) cerr << " Reading FM Index from file " << fmi_filename << endl;
	FILE * fp = fopen(fmi_filename.c_str(),"r");
	if (!fp) { cerr << "Could not open file " << fmi_filename << endl; usage(argv[0]); }
  BWT * b = readIndexes(fp);
	fclose(fp);
	if(debug) fprintf(stderr,"BWT of length %ld has been read with %d sequencs, alphabet=%s\n", b->len,b->nseq, b->alphabet); 

	config->bwt = b;
	config->fmi = b->f;

	config->init();

	if(output_filename.length()>0) {
		cerr << "Output file: " << output_filename << endl;
		ofstream * read2id_file = new ofstream();
		read2id_file->open(output_filename);    
		if(!read2id_file->is_open()) {  cerr << "Could not open file " << output_filename << " for writing" << endl; exit(EXIT_FAILURE); }
		config->out_stream = read2id_file;
	}
	else {
		config->out_stream = &cout;
	}

	ProducerConsumerQueue<ReadItem*>* myWorkQueue = new ProducerConsumerQueue<ReadItem*>(500);        
	std::deque<std::thread> threads;
	std::deque<ConsumerThreadp *> threadpointers;
	for(int i=0; i < num_threads; i++) {
		ConsumerThreadp* p = new ConsumerThreadp(myWorkQueue, config);
		threadpointers.push_back(p);
		threads.push_back(std::thread(&ConsumerThreadp::doWork,p));
	}

	ifstream in1_file, in2_file;
	in1_file.open(in1_filename.c_str());    
	
	if(!in1_file.is_open()) {  cerr << "Could not open file " << in1_filename << endl; exit(EXIT_FAILURE); }
	if(in2_filename.length() > 0) {
		in2_file.open(in2_filename.c_str());    
		if(!in2_file.is_open()) {  cerr << "Could not open file " << in2_filename << endl; exit(EXIT_FAILURE); }
		paired = true;
	}

	
	bool isFastQ = false;  
	string line_from_file;
	line_from_file.reserve(2000);
	{
		getline(in1_file,line_from_file);
		char fileTypeIdentifier = line_from_file.front();
		if(fileTypeIdentifier == '@')
			isFastQ = true;
		else if(fileTypeIdentifier != '>') {
			cerr << "Auto-detection of file type for file " << in1_filename << " failed."  << endl; 
			exit(EXIT_FAILURE); 
		}
		in1_file.seekg(0, ios::beg);//set pointer to file start again
	}
	
	string name;
	string sequence1;
	sequence1.reserve(2000);

	if(verbose) cerr << getCurrentTime() << " Start search using " << num_threads << " threads." << endl;

	while(getline(in1_file,line_from_file)) {                		
		if(isFastQ) {
			// remove '@' from beginning of line
			line_from_file.erase(line_from_file.begin());
			name = line_from_file;
			// read sequence line
			getline(in1_file,line_from_file);
			// remove non-alphabet chars
			strip(line_from_file);
			sequence1 = line_from_file;
			// skip + line	
			in1_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			// skip quality score line
			in1_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		}
		else { //Fasta
			// remove '>' from beginning of line
			line_from_file.erase(line_from_file.begin());
			name = line_from_file;
			// read lines until next entry starts or file terminates
			sequence1.clear();
			while(!(in1_file.peek()=='>' || in1_file.peek()==EOF)) {
				getline(in1_file,line_from_file);
				sequence1.append(line_from_file);
			}
			strip(sequence1);
		} // end Fasta
		myWorkQueue->push(new ReadItem(name, sequence1));

	} // end main loop around file1

	if(paired) {
		//search second file
		while(getline(in2_file,line_from_file)) {                		
			if(isFastQ) {
				// remove '@' from beginning of line
				line_from_file.erase(line_from_file.begin());
				name = line_from_file;
				// read sequence line
				getline(in1_file,line_from_file);
				// remove non-alphabet chars
				strip(line_from_file);
				sequence1 = line_from_file;
				// skip + line	
				in1_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
				// skip quality score line
				in1_file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			}
			else { //Fasta
				// remove '>' from beginning of line
				line_from_file.erase(line_from_file.begin());
				name = line_from_file;
				// read lines until next entry starts or file terminates
				sequence1.clear();
				while(!(in1_file.peek()=='>' || in1_file.peek()==EOF)) {
					getline(in1_file,line_from_file);
					sequence1.append(line_from_file);
				}
				strip(sequence1);
			} // end Fasta

			myWorkQueue->push(new ReadItem(name, sequence1));

		} // end main loop around file1

	}

	myWorkQueue->pushedLast();    	

	if(in1_file.is_open()) in1_file.close();
	if(paired && in2_file.is_open()) in2_file.close();

	while(!threads.empty()) {
		threads.front().join();
		threads.pop_front();
		delete  threadpointers.front();
		threadpointers.pop_front();
	}
	if(verbose) cerr << getCurrentTime() << " Finished." << endl;
	
	config->out_stream->flush();
	if(output_filename.length()>0) {
		((ofstream*)config->out_stream)->close();
		delete ((ofstream*)config->out_stream);
	}

	delete myWorkQueue;
	delete config;
	return EXIT_SUCCESS;    
}

inline bool isalpha(char & c) { 
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

void strip(string &s) {
		for(auto it = s.begin(); it!=s.end(); ++it) {
			if(!isalpha(*it)) {
				s.erase(it);
				it--;
			}
		}
}

void usage(char *progname) { 
	fprintf(stderr, "Usage:\n   %s -f allproteins.fmi -i reads.fastq [-j reads2.fastq]\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Mandatory arguments:\n");
	fprintf(stderr, "   -f FILENAME   Name of .fmi file\n");
	fprintf(stderr, "   -i FILENAME   Name of input file containing reads in FASTA or FASTQ format\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "Optional arguments:\n");
	fprintf(stderr, "   -j FILENAME   Name of second input file for paired-end reads\n");
	fprintf(stderr, "   -o FILENAME   Name of output file. If not used, then output will be printed to STDOUT\n");
	fprintf(stderr, "   -z INT        Number of parallel threads (default: 1)\n");
	fprintf(stderr, "   -a STRING     Run mode, either \"mem\"  or \"greedy\" (default: mem)\n");
	fprintf(stderr, "   -e INT        Number of mismatches allowed (default: 0)\n");
	fprintf(stderr, "   -m INT        Minimum match length in MEM mode (default: 11)\n");
	fprintf(stderr, "   -s INT        Minimum match score in Greedy mode (default: 65)\n");
	fprintf(stderr, "   -v            Enable verbose output.\n");
	fprintf(stderr, "   -d            Enable debug output.\n");
//fprintf(stderr, "   -l <UINT> Seed length for finding matches in smartfast mode (default: 7)\n");
	exit(EXIT_FAILURE);
}

string getCurrentTime() {
	time_t t= time(0);
	char buffer[9] = {0};
	strftime(buffer, 9, "%H:%M:%S", localtime(&t));
	return string(buffer);  
}
