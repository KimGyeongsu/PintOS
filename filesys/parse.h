#ifndef FILESYS_PARSE_H
#define FILESYS_PARSE_H

#include "filesys/inode.h"

struct parsing_result {
	bool valid;
	char* filename; //부모 directory에서 찾을 filename asad, ., ..
	struct inode *dir_inode; //부모 directory의 inode 
	//이친 구는 valid==false 이거나 is_root == true일 때 parse 함수에서 무조건 close 해주자
	bool is_dir; //directory인가 check
	bool is_root; //root directory면

};

struct parsing_result *parse(char *name, bool flag);
struct parsing_result *parse_mkdir(char *name);
#endif /* filesys/file.h */
