[![install with bioconda](https://img.shields.io/badge/install%20with-bioconda-brightgreen.svg?style=flat)](http://bioconda.github.io/recipes/aletsch/README.html)
[![Anaconda-Server Badge](https://anaconda.org/bioconda/aletsch/badges/downloads.svg)](https://anaconda.org/bioconda/aletsch)

# Introduction

Aletsch implements an efficient algorithm to assemble multiple RNA-seq samples (or multiple cells
for single-cell RNA-seq data).
The datasets and scripts used to compare the performance of Aletsch with other assemblers are available at
[aletsch-test](https://github.com/Shao-Group/aletsch-test).

<!--
It uses splice graph and phasing paths as underlying data strctures to represent
the alignments of each gene loci in individual RNA-seq samples.
Efficient algorithms are implemented to combine splice graphs (and phasing paths)
at overlapped gene loci. Eventually, the core algorithm used in Scallop (i.e., phase-preserving decomposition)
is employed to decompose the combined splice graphs to transcripts.
-->


# Version v1.1.1

We released Aletsch [v1.1.1](https://github.com/Shao-Group/aletsch/releases/tag/v1.1.1),
a version that substantially improved 
the memory usage and running time over its previous version [v1.1.0](https://github.com/Shao-Group/aletsch/releases/tag/v1.1.0), 
while maintaining an identical assembly accuracy.
The improvement was primarily made by fixing the incorrect use of bam-file queries
and by removing PCR duplicates.
Below we detail the memory usage and running time, both CPU-time and Wall-time (10 threads), of the two versions
across all datasets we tested (see [aletsch-test](https://github.com/Shao-Group/aletsch-test)).

### Memory Usage Comparison (in GB):

| Dataset | v1.1.1 | v1.1.0 |
| :-----: | ------ | ------ |
|  BK-H1  | 6.96   | 35.55  |
|  BK-H2  | 11.79  | 64.44  |
|  BK-H3  | 5.32   | 34.35  |
|  BK-M1  | 21.47  | 168.01 |
| SC-H1&3 | 4.12   | 47.23  |
|  SC-H2  | 24.43  | 251.81 |
|  SC-M1  | 9.27   | 82.93  |

### CPU And Wall-Clock Time Comparison (in minutes):

| Dataset | v1.1.1(CPU) | v1.1.1(Wall) | v1.1.0(CPU) | v1.1.0(Wall) |
| :-----: | :---------: | :----------: | :---------: | :----------: |
|  BK-H1  |     219     |      27      |     541     |      53      |
|  BK-H2  |     923     |      96      |    1319     |     135      |
|  BK-H3  |     155     |      17      |     258     |      28      |
|  BK-M1  |     691     |      73      |    1464     |     169      |
| SC-H1&3 |     186     |      21      |     167     |      20      |
|  SC-H2  |    1077     |     129      |    1530     |     183      |
|  SC-M1  |     382     |      44      |     441     |      52      |

# Installation

Aletsch can be installed through [conda](https://anaconda.org/bioconda/aletsch)
or by compiling source (see [INSTALLATION](https://github.com/Shao-Group/aletsch/blob/master/INSTALLATION.md)).

# Usage

The usage of `aletsch` is:
```
./aletsch -i <input-bam-list> -o <output.gtf> [options]
```

We highly recommend to generate profiles for individual samples first:
```
./aletsch --profile -i <input-bam-list> -p <profile>
./aletsch -i <input-bam-list> -o <output.gtf> -p <profile> [options]
```

## Format of Input and Output
Each line of `input-bam-list` describes a single sample, with 3 fields separated by space.
The 3 fields are: `alignment-file` (in .bam format), `index-alignment-file` (in. bai format), and `protocol`.
The `index-file` can be generated using samtools (e.g., `samtools index ...`).
The `protocol` is chosen from the 5 options: `single_end` (for illumina single-end RNA-seq protocol),
`paired_end` (for illumina paired-end RNA-seq protocol), 
`pacbio_ccs` (for PacBio Iso-Seq CCS reads),
`pacbio_sub` (for PacBio Iso-Seq sub-reads),
`ont` (for Oxford Nanopore RNA-seq).
Aletsch will use different parameters / algorithms to process different data types.

Aletsch requires that each input alignment file is sorted; otherwise run `samtools` to sort it (`samtools sort input.bam > input.sort.bam`).

The assembled transcripts from all these samples will be written to `output.gtf`, in standard .gtf format.

## Options

Aletsch provides several options for transcript assembly, supporting both its unique parameters and those required by the core algorithm of Scallop. For a detailed list, execute `aletsch` without arguments.

| Parameters | Type    | Default Value | Description                                                  |
| ---------- | ------- | ------------- | ------------------------------------------------------------ |
| --help     |         |               | Displays Aletsch usage information and exits.                |
| --version  |         |               | Shows Aletsch version information and exits.                 |
| --profile  |         |               | Profiles individual samples and exits. Writes to files if `-p` is specified. |
| -l         | string  |               | Specifies chromosomes to assemble.                           |
| -L         | string  |               | Specifies a file containing a list of chromosomes to assemble. |
| -d         | string  |               | Output directory for individual sample transcripts. Directory must exist prior to execution. |
| -p         | string  |               | Directory for reading/saving individual sample profiles. Directory must exist prior to execution. |
| -t         | integer | 10            | Number of threads.                                           |
| -c         | integer | 200           | Maximum number of splice graphs in a cluster, recommended as twice the number of samples. |
| -s         | float   | 0.2           | Minimum similarity for combining two splice graphs.          |

* If `-l string` or `-L file` option is provided, Aletsch assembles only the specified chromosomes; otherwise, it assembles all chromosomes.

<!--
| -b         | string  |               | Output directory for bridged alignment files. Directory must exist prior to execution. |
-->

- Directories specified by `-d` and `-p` must exist before running Aletsch; the tool does not create directories.
- With `--profile`, Aletsch infers profiles of individual samples, using the `XS` tag from input BAM files.

# Scoring Transcripts with Pre-trained Model

Aletsch employs a random forest model for scoring transcripts, available for download from [Zenodo](https://doi.org/10.5281/zenodo.10602529). Use the provided Python script `score.py` with this model.

## Dependencies

Required Python libraries: numPy, pandas, scikit-learn, joblib

- Using pip:

  ```bash
  pip install numpy pandas scikit-learn joblib
  ```

- Using conda (recommended):

  ```bash
  conda install numpy pandas scikit-learn joblib
  ```

## Usage

Score transcripts with the syntax below:

```
python3 score.py -i <individual_gtf_dir> -m <pretrained_model.joblib> -c <num_of_samples> -p <min_probability_score> -o <output_score.csv>
```

| Parameter | Type    | Default | Description                                                  |
| --------- | ------- | ------- | ------------------------------------------------------------ |
| `-i`      | String  |         | Directory containing Aletsch's feature files. This is the same directory where Aletsch outputs individual GTF files, as designated by the `-d` option in Aletsch's assembly process. |
| -m        | String  |         | Path to the pre-trained model file for scoring.              |
| -c        | Integer |         | Number of samples/cells                                      |
| -p        | String  | 0.2     | Minimum probability score threshold (range: 0 to 1).         |
| -o        | String  |         | Output directoty of scored .csv file.                        |
