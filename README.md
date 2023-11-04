# VVSFS

An implementation of a very very simple filesystem built for a university group project.

Authors:

* @EngineersBox
* @CraftyDH
* @angussidney

# Problem description 

A simplistic filesystem has been created, the vvsfs (see the directory [vvsfs](./vvsfs/)). It plugs into the linux VFS and may be loaded as a kernel module. This filesystem is deficient in a variety of ways.  Your task is to modify/improve/extend this filesystem to explore possible approaches and implementations of filesystems. 

There are three levels of difficulties in this assignment, and your grade will be determined based on the level of difficulties of the task you completed. 

## Baseline (70 points).

At this level, you are expected to make the following improvement to VVSFS:

* enable the filesystem to remove files and directories. 
* enable the filesystem to rename files and directories.
* enable the filesystem to store various inode attributes, including owner id, group id, change/access/modification time.
* entable the filesystem to report various information such as the number of blocks, the number of free blocks, the number of free indoes, etc. (In other words, complete the `vvsfs_statfs()`` function in the super operations).

The maximum points allocated for completing these baseline tasks are 70. 

## Advanced (+20 points).

For this level, you are expected to introduce an indirect pointers block to the data blocks of an inode. In the current version of VVSFS, there are 15 blocks allocated for direct pointers. Modify this so that 14 blocks are allocated to direct pointers and 1 remaining block (the last block in the `i_block[]` member of `vvsfs_inode`) is designated as an indrect pointers block. So this last block should point to another data block, that contains direct pointers to data blocks. 
All the required Baseline level functionalities must also work with the modified data structure. 

A successful completion of this additional task will gain you at most 20 additional points. 

## Extension (+10 points). 

For this level, you are expected to implement all the required functionalities at the Advanced level, plus one or more of the following features:

* Add the ability to create hard links and symbolic links. 

* Add the ability to create a special (device) files, e.g., a block or a character device file. 

* Implement a filesystem encryption, allowing the filesystem to be locked/unlocked with a password. 

* Incorporate some form of compression to reduce the amount of disk space used. 

* Make the filesystem more robust to data corruption.

* Implement a defragmentation tool for the filesystem.

* Implement a filesystem checker to check and (possibly) repair the filesystem. 

* Implement the filesystem in Rust. :)

* Think up your own extesion!

Completing one or more of these extension tasks will gain you an additional 10 points. 


# Working on the assignment

* This assignment is a groupd-based assignment, with 3 members per group. Group registration must be done on Wattle (see the Wattle page for this course for more information). 

* To get started have one member of your team "fork" this assignment repo and add the other members of your team as a __maintainer__ to the repo.

    * **Make sure** that the 'visibility' of your fork is set to **private**.
    * **Make sure** that you select _your_ namespace. (this is only applicable to students who have greater than normal Gitlab access - others will only be able to select their own namespace.)
    * **DO NOT RENAME THE PROJECT - LEAVE THE NAME AND URL SLUG EXACTLY AS IS**
    * You may notice an additional member in your project - `comp3300-2023-s2-marker`. **Do not remove this member from your project**.


* Make sure to commit and push to the Gitlab regularly to save your work.

* You may add and write your report source document (`.docx`, `.md`, whatever) to this repository if you wish

* Add your report PDF to this repository with the name "GroupXX_report.pdf" where XX is your group number as appeared on Wattle. **You will still have to submit your PDF report to Turnitin**.


# Submission 

There are two parts to this assignment that you must submit:

* Artefact (code): this is your implementation of the filesystem and must be submitted through gitlab. 

* Report (pdf): This must be uploaded to Turnitin, **and** committed and pushed to gitlab. 

We will use the gitlab submission time to determine whether you submitted on time. However, your mark will not be finalised until you have also submitted your report to Turnitin for validation. 

## Late submission policy

**No late submission allowed** without a prior approval from the convenor. Late submission penalty is 100%. 

## Statement of originality

- You must include a Statement of Originality in your report. Use the following template for the statement:

    >   We declare that everything we have submitted in this assignment is entirely our
  own work, with the following exceptions:
    >  - list the sources of code you used, articles, blogs, books etc, if any
    >  - discussions with others related to this assignment, if any 


- Read the [ANU page on academic integrity](https://www.anu.edu.au/students/academic-skills/academic-integrity) for more information on academic integrity. 



# Assessment guideline

Relative to the level of accomplishment (i.e., Baseline, Advanced and Extensions as described above), the assignment will be marked based on the following components:

* Artefact (50%):

    * (40%) Functionalities implemented and working correctly. 

    * (10%) Code quality: 
        - Code cleary formatted and commented
        - Compiled without errors or warnings
        - Good coding practice

* Report (40%):
    * Completeness: all required functionalities explained.
    * Correctness: explanation of algorithm(s)/data structure(s) used is correct. 
    * Writing quality: 
        - Your writing should be concise, readable and comprehensible. Focus on important parts of your code and explain key ideas in the code. Don't waste space by explaining every line of code -- for that you can use comments in the source file. 
        - Your report should be professionally typeset, so for example, avoid handwritten notes, and if you use screen captures, they must be readable. 
        - Your report should contain proper acknowledgement/citations if you use any external sources for your ideas. 
    * Report length <= 2500 words, excluding citations, figures, code excerpts, tables. 

* Presentation (10%)



## Further information 

If necessary, further information and hints will be provided in the Ed discussion forum for this course.  A Q&A thread will be posted on Ed to respond to frequently asked questions. 
