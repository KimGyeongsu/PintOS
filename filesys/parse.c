#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/parse.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>




static struct parsing_result *update_result_relative_open(
                struct parsing_result *result, char* name,int start, int end);
static struct parsing_result *update_result_absolute_open(
                struct parsing_result *result, char* name,int start, int end, bool must_dir);
static struct parsing_result *parse_open(char *filename);

static struct parsing_result *update_result_relative_create(
                struct parsing_result *result, char* name,int start, int end);
static struct parsing_result *update_result_absolute_create(
                struct parsing_result *result, char* name,int start, int end, bool must_dir);
static struct parsing_result *parse_create(char *filename);



struct parsing_result *parse(char *name, bool flag)
{
  if(flag==false) return parse_open(name);
  else return parse_create(name);
}



static struct parsing_result *parse_open(char *filename)
{
  struct parsing_result *result = malloc(sizeof(struct parsing_result));
  result->valid = false;
  result->is_dir = false;
  result->is_root = false;
  // filename NULL : false
  if(filename==NULL || strlen(filename)==0)
  {
    result->valid = false;
    return result;
  }
  // 젤 마지막 문자가 / 일 경우, ex : cd a/b/ -> parsing 하면 a 와 b 로 나누어 질 것임. 이 경우 b는 무조건 directory여야 함
  // 이걸 체크하기 위한 flag
  bool must_dir;
  if(filename[strlen(filename)]=='/') must_dir = true;
  else must_dir = false;
  //크게 케이스 4가지 
  //절대경로 : 첫 문자가 (1) /
  //상대경로 (2) .. (3) ./a  == (4) a
  bool absolute ;
  if(filename[0]=='/') absolute = true;
  else absolute = false;
  ////////////////////////////////////////////////////parsing
  int num=8;
  char **arg_tokens = malloc(num); //it must free
  int argc=0;
  char *token, *save_ptr;

  for(token = strtok_r(filename, "/", &save_ptr); token !=NULL; token = strtok_r (NULL, "/", &save_ptr))
  {
    if(argc*8>=num)
    {
      num=num*2; 
      arg_tokens = realloc(arg_tokens, num);
    }
    arg_tokens[argc]=token;
    argc++;
  }
  arg_tokens[argc]='\0';
  save_ptr = NULL;
  //arg_tokens : parsing 넣어둔거, argc : 길이
  ////////////////////////////////////////////////////parsing done
  // 특수 케이스 : argc 가 0일 때 :: 즉 cd /////// 이런 거. : root directory
  if(argc == 0)
  {
    result->valid = true;
    result->is_root = true;
    result->is_dir = true;
    free(arg_tokens);
    return result;
  }
  ///////////////////////////////////////절대경로 case (1)
  if(absolute)
  {
    if(argc==1) // 이경우 case(1-1) cd ///.., case(1-2) cd ///., case(1-3) cd ///a
    {
      if(arg_tokens[0]=="." || arg_tokens[0]=="..") //case (1-1), case (1-2)
      {
        result->valid = true;
        result->is_root = true;
        result->is_dir = true;
        free(arg_tokens);
        return result;
      }
      else //case (1-3)
      { 
        struct dir *dir = dir_open_root(); //open  root directory
        struct inode *inode = NULL;

        if(must_dir) //if it must be directory
        {
          if(dir_lookup(dir,arg_tokens[0],&inode)==true) // if find it
          {
            if(inode->is_dir==false) //it isn't directory
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
              free(arg_tokens);
              return result;
            }
            else //this is directory
            {
              dir_close(dir);
              inode_close(inode);
              result->valid = true;
              result->dir_inode = inode_open(1); //parent directory is root directory
              result->is_dir = true;
              result->filename = arg_tokens[0];
              free(arg_tokens);
              return result;
            }
          }
          else //coudn't find it
          {
            dir_close(dir);
            result->valid = false;
            free(arg_tokens);
            return result;
          }
        }
        else //it could be either directory or file
        {
          if(dir_lookup(dir,arg_tokens[0],&inode)==true) // if find it
          {
            if(inode->is_dir==false) //it is file
            {
              dir_close(dir);
              inode_close(inode);
              result->valid = true;
              result->dir_inode = inode_open(1); //parent directory is root directory
              result->is_dir = false;
              result->filename = arg_tokens[0];
              free(arg_tokens);
              return result;
            }
            else //this is directory
            {
              dir_close(dir);
              inode_close(inode);
              result->valid = true;
              result->dir_inode = inode_open(1); //parent directory is root directory
              result->is_dir = true;
              result->filename = arg_tokens[0];
              free(arg_tokens);
              return result;
            }
          }
          else //coudn't find it
          {
            dir_close(dir);
            result->valid = false;
            free(arg_tokens);
            return result;
          }
        }
      }
    }
    ///////////////////////now argc >= 2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") // case (1) : cd ////./something or cd ////../something
        {
          result->dir_inode = inode_open(1); //parent directory : root directory
          result->valid = true;
        }
        else  //case (2) cd ////a/something
        {
          struct dir *dir = dir_open_root(); //open root directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at root directory
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
              }
            }
          else /// if a couldn't find at root directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_open(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_open(result, arg_tokens[i], i, argc-1, must_dir);
      }
    }
    free(arg_tokens);
    return result;
  }
  ////////////////////////////////NOW start with 상대경로로 시작하는 case (2) .. (3) ./a  == (4) a
  if(argc == 1) //length ==1 
  {
    //case (2) cd . case (3) cd .. case (4) cd a
    if(arg_tokens[0]==".") //case(2)
    {
      result->valid = true;
      result->is_dir = true;
      block_sector_t cur_sector = thread_current()->cur_dir_sector;
      if(cur_sector==1) //return root directory
      {
        result->is_root = true;
        free(arg_tokens);
        return result;
      }
      // the current directory is no more root directory(return result is not root directory)
      struct dir *cur_dir = dir_open(inode_open(cur_sector)); // open current directory
      block_sector_t parent_sector = cur_dir->inode->prev_sector;
      struct dir *parent_dir = dir_open(inode_open(parent_sector)); //open parent directory
      result->filename = find_filename(parent_dir, cur_sector);
      dir_close(parent_dir);
      dir_close(cur_dir);
      result->dir_inode = inode_open(parent_sector);
      free(arg_tokens);
      return result;
    }
    if(arg_tokens[0] =="..") //case (3)
    {
      result->valid = true;
      result->is_dir = true;
      block_sector_t cur_sector = thread_current()->cur_dir_sector;

      if(cur_sector==1) //return root directory
      {
        result->is_root = true;
        free(arg_tokens);
        return result;
      } 
      // the current directory is no more root directory
      struct dir *cur_dir = dir_open(inode_open(cur_sector)); // open current directory
      block_sector_t parent_sector = cur_dir->inode->prev_sector;
      if(parent_sector==1) //return root directory
      {
        result->is_root = true;
        dir_close(cur_dir);
        free(arg_tokens);
        return result;
      }
      /////////////////////////now return result is not root directory
      struct dir *parent_dir = dir_open(inode_open(parent_sector)); //open parent directory
      block_sector_t grandparent_sector = parent_dir->inode->prev_sector;
      struct dir *grandparent_dir = dir_open(grandparent_sector); //grand parent directory
      result->filename = find_filename(grandparent_dir, parent_sector);
      dir_close(grandparent_dir);
      dir_close(parent_dir);
      dir_close(cur_dir);
      result->dir_inode = inode_open(grandparent_sector);
      free(arg_tokens);
      return result;
    }
    /////////case (4) something/a
    block_sector_t cur_sector = thread_current()->cur_dir_sector;
    struct dir *dir = dir_open(inode_open(cur_sector)); //open something directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, arg_tokens[0], &inode)==true) //a must exists at something directory (find success)
    {
      if(inode->is_dir==false && must_dir == true) // it is file, but it must be directory
      {
        result->valid = false;
        dir_close(dir);
        inode_close(inode);
        free(arg_tokens);
        return result;
      }
      else if(inode->is_dir==false && must_dir == false) // it is file and it dosen't matter whther it is file or not
      {
        dir_close(dir);
        inode_close(inode);
        result->valid = true;
        result->dir_inode = inode_open(cur_sector); //parent directory is something directory
        result->is_dir = false;
        result->filename = arg_tokens[0];
        free(arg_tokens);
        return result;
      }
      else //it is directory
      {
        dir_close(dir);
        inode_close(inode);
        result->valid = true;
        result->dir_inode = inode_open(cur_sector); //parent directory is something directory
        result->is_dir = true;
        result->filename = arg_tokens[0];
        free(arg_tokens);
        return result;
      }
    }
    else /// if a couldn't find at root directory
    {
      dir_close(dir);
      result->valid = false;
      free(arg_tokens);
      return result;
    }
  }
  //////////////////////////////////////NOW argc>=2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]==".") // case (1) : cd ./something
        {
          result->dir_inode = inode_open(thread_current()->cur_dir_sector); //parent directory : current directory
          result->valid = true;
        }
        else if(arg_tokens[i]!="..")  //case (2) cd a/something
        {
          struct dir *dir = dir_open(inode_open(thread_current()->cur_dir_sector)); //parent directory : current directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at parent
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
            }
          }
          else /// if a couldn't find at parent directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
        else //case (3) cd ../something
        {
          struct inode *inode = inode_open(thread_current()->cur_dir_sector);
          result->valid = true;
          if(inode->sector == 1)
          {
            result->dir_inode = inode;
          }
          else
          {
            result->dir_inode = inode_open(inode->prev_sector); //parent directory : parent of current directory;
            inode_close(inode);
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_open(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_open(result, arg_tokens[i], i, argc-1, must_dir);
      }
    }
  //////////////done
  free(arg_tokens);
  return result;
}

static struct parsing_result *update_result_relative_open(
                struct parsing_result *result, char* name,int start, int end) //start : 무조건 2번째 index(1) 이상
{
  if(result->valid ==false) return result;
  if(start!=end) //끝이 아니라면
  {
    if(name==".") //case(1) something A/./something B
    {
      return result;
    }
    if(name=="..") //case(2) something A/../something B
    {
      if(result->dir_inode->sector == 1) return result; //if something A == root directory
      //if something A != root directory
      struct inode *inode;
      inode = inode_open(result->dir_inode->prev_sector); //previous directory inode open
      inode_close(result->dir_inode); //current directory inode close
      if(inode->is_dir==false) PANIC("PARENT DIRECTORY's inode isn't directory"); //and previsou directory must directory
      result->dir_inode = inode;
      return result;
    }
  }
  if(start==end) //case(1) : something/. or case(2) : something/..
  {
    if(result->dir_inode->sector == 1) //both case : if something is root directory
    {
      inode_close(result->dir_inode); //close inode
      result->valid = true;
      result->is_root = true;
      result->is_dir = true;
      return result;
    }
    ///////////////////////////////////now something is not root directory
    if(name==".") //case(1) cd ~~~~~~/. (return value is not root directory)
    {
      result->valid = true;
      result->is_dir = true;

      block_sector_t parent_dir_inode_number = result->dir_inode->prev_sector;
      struct dir *parent_dir = dir_open(inode_open(parent_dir_inode_number)); //open parent of something directory

      result->filename = find_filename(parent_dir, result->dir_inode->sector);
      dir_close(parent_dir); //close parent of something directory;
      inode_close(result->dir_inode); //close something directory's inode
      result->dir_inode = inode_open(parent_dir_inode_number); //set parent directory's inode
      return result;
    }
    if(name=="..") //case(2) cd something/..
    {
      result->valid = true;
      result->is_dir = true;
      block_sector_t parent_dir_inode_number = result->dir_inode->prev_sector;
      
      if(parent_dir_inode_number == 1) // case (2-1) if cd something/.. is equal to cd root
      {
        inode_close(result->dir_inode); //close inode
        result->is_root = true;
        return result;
      }
      //case (2-2) the return value is not root directory
      struct dir *parent_dir = dir_open(inode_open(parent_dir_inode_number)); //open parent of something directory
      block_sector_t grandparent_dir_inode_number = parent_dir->inode->prev_sector;
      struct dir *grandparent_dir = dir_open(grandparent_dir_inode_number); //grand parent of something directory

      result->filename = find_filename(grandparent_dir, parent_dir_inode_number);
      dir_close(parent_dir); //close parent of something directory;
      dir_close(grandparent_dir); //close grandparent of something direcotory
      inode_close(result->dir_inode); //close something directory's inode
      result->dir_inode = inode_open(grandparent_dir_inode_number); //set parent directory's inode
      return result;
    }
  }
}

static struct parsing_result *update_result_absolute_open(
                struct parsing_result *result, char* name,int start, int end, bool must_dir) //start : 무조건 2번째 index(1) 이상
//name isn't . or ..
{
  if(result->valid ==false) return result;
  if(start!=end) //끝이 아니라면 ex : cd  soemthing A/name/something B즉 name must be directory
  {
    struct dir *dir = dir_open(result->dir_inode); //open something A directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, name, &inode)==true) // find name success
    {
      if(inode->is_dir==false) //but it is not directory
      {
        result->valid = false;
        dir_close(dir);
        inode_close(inode);
        return result;
      }
      else //and it is directory
      {
        result->valid = true;
        result->dir_inode = inode;
        dir_close(dir);
        return result;
      }
    }
    else //find name success fail
    {
      dir_close(dir);
      result->valid = false;
      return result;
    }   
  }
  ////////////////////////////// start == end : ex) cd something/name
  if(must_dir) //case (1) name must be directory
  {
    struct dir *dir = dir_open(result->dir_inode); //open something directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, name, &inode)==true) //find name success
    {
      if(inode->is_dir==false) // it isn't directory
      {
        result->valid = false;
        dir_close(dir);
        inode_close(inode);
        return result;  
      }
      else // it is directory
      {
        block_sector_t parent_dir_inode_number = result->dir_inode->sector; //something directory inode number
        result->valid = true;
        result->filename = name;
        dir_close(dir);
        inode_close(inode);
        result->is_dir = true;
        result->dir_inode = inode_open(parent_dir_inode_number);
        return result;
      }
    }
    else //find name fail
    {
      dir_close(dir);
      result->valid = false;
      return result;
    }
  }
  //////////////////////////////////case(2) it doesn't matter
  struct dir *dir = dir_open(result->dir_inode); //open something directory
  struct inode *inode = NULL;
  if(dir_lookup(dir, name, &inode)==true) //find name success
  {
    if(inode->is_dir==false) // it is file
    {
      block_sector_t parent_dir_inode_number = result->dir_inode->sector; //something directory inode number
      result->valid = true;
      result->filename = name;
      dir_close(dir);
      inode_close(inode);
      result->is_dir = false; // it is file
      result->dir_inode = inode_open(parent_dir_inode_number);
      return result; 
    }
    else // it is directory
    {
      block_sector_t parent_dir_inode_number = result->dir_inode->sector; //something directory inode number
      result->valid = true;
      result->filename = name;
      dir_close(dir);
      inode_close(inode);
      result->is_dir = true; //it is directory
      result->dir_inode = inode_open(parent_dir_inode_number);
      return result;
    }
  }
  else //find name fail
  {
    dir_close(dir);
    result->valid = false;
    return result;
  }
}


//////////////////////////////////parse create
static struct parsing_result *parse_create(char *filename)
{
  //printf("NO?\n");
  struct parsing_result *result = malloc(sizeof(struct parsing_result));
  result->valid = false;
  result->is_dir = false;
  result->is_root = false;
  // filename NULL : false
  if(filename==NULL||strlen(filename)==0)
  {
    result->valid = false;
    return result;
  }
 // printf("parse_create enter\n");
  //printf("filename %s\n", filename);
  // 젤 마지막 문자가 / 일 경우, ex : cd a/b/ -> parsing 하면 a 와 b 로 나누어 질 것임. 이 경우 b는 무조건 directory여야 함
  // 이걸 체크하기 위한 flag -> create에서는 return->is_dir flag를 설정하기 위해서 확인
  bool must_dir;
  if(filename[strlen(filename)]=='/') must_dir = true;
  else must_dir = false;
  //크게 케이스 4가지 
  //절대경로 : 첫 문자가 (1) /
  //상대경로 (2) .. (3) ./a  == (4) a
  bool absolute ;
  if(filename[0]=='/') absolute = true;
  else absolute = false;
 // printf("must_dir %d absolute %d\n", must_dir,absolute);
  ////////////////////////////////////////////////////parsing
 // printf("parsing?\n");
  int num=8;
  char **arg_tokens = malloc(num); //it must free
  int argc=0;
  char *token, *save_ptr;
  //printf("filename %s\n", filename);
  //printf("parsed result %s\n", strtok_r(filename, "/", &save_ptr));
  save_ptr = NULL;

  for(token = strtok_r(filename, "/", &save_ptr); token !=NULL; token = strtok_r (NULL, "/", &save_ptr))
  {
    //printf("token1 %s\n",token);
    if(argc*8>=num)
    {
      //printf("aa\n");
      num=num*2; 
      arg_tokens = realloc(arg_tokens, num);
    }
    //printf("argc\n");
    arg_tokens[argc]=token;
    argc++;
  }
  //printf("aaa\n");
  save_ptr =NULL;
  /*
     Example usage:

   char s[] = "  String to  tokenize. ";
   char *token, *save_ptr;

   for (token = strtok_r (s, " ", &save_ptr); token != NULL;
        token = strtok_r (NULL, " ", &save_ptr))
     printf ("'%s'\n", token);

   outputs:

     'String'
     'to'
     'tokenize.'*/
  //printf("parsingdone?\n");
  arg_tokens[argc]='\0';
  //arg_tokens : parsing 넣어둔거, argc : 길이
  ////////////////////////////////////////////////////parsing done
  // 특수 케이스 : argc 가 0일 때 :: 즉 cd /////// 이런 거. : root directory create
  //printf("argc %d arg_tokens[0] %s arg_tokens[1] %s\n", argc, arg_tokens[0],arg_tokens[1]);
  if(argc == 0)
  {
    result->valid = true;
    result->is_root = true;
    result->is_dir = true;
    free(arg_tokens);
    return result;
  }
  ///////////////////////////////////////절대경로 case (1)
  if(absolute)
  {
    if(argc==1) // 이경우 case(1-1) cd ///.., case(1-2) cd ///., case(1-3) cd ///a
    {
      if(arg_tokens[0]=="." || arg_tokens[0]=="..") //case (1-1), case (1-2) : root directory create
      {
        result->valid = true;
        result->is_root = true;
        result->is_dir = true;
        free(arg_tokens);
        return result;
      }
      else //case (1-3)
      { 
        struct dir *dir = dir_open_root(); //open  root directory
        struct inode *inode = NULL;
        ////////////////////////////////////////have to check a is exist or not
        if(dir_lookup(dir,arg_tokens[0],&inode)==true) // if find it -> cannot create
        {
          dir_close(dir);
          inode_close(inode);
          result->valid  = false;
          free(arg_tokens);
          return result;
        }
        else //coudn't find it ->can create
        {
          dir_close(dir);
          result->valid = true;
          result->dir_inode = inode_open(1); //parent directory is root directory
          result->is_dir = must_dir;
          result->filename = arg_tokens[0];
          free(arg_tokens);
          return result;
        }
      }
    }
    ///////////////////////now argc >= 2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") // case (1) : cd ////./something or cd ////../something
        {
          result->dir_inode = inode_open(1); //parent directory : root directory
          result->valid = true;
        }
        else  //case (2) cd ////a/something
        {
          struct dir *dir = dir_open_root(); //open root directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at root directory
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
              }
            }
          else /// if a couldn't find at root directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_create(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_create(result, arg_tokens[i], i, argc-1, must_dir);
      }
    }
    free(arg_tokens);
    return result;
  }
  ////////////////////////////////NOW start with 상대경로로 시작하는 case (2) .. (3) ./a  == (4) a
  if(argc == 1) //length ==1 
  {
    //case (2) cd . case (3) cd .. case (4) cd a
    if(arg_tokens[0]=="." || arg_tokens[0]=="..") //case(2) or (3) : cannot create
    {
      result->valid = false;
      free(arg_tokens);
      return result;
    }
    /////////case (4) something/a

    block_sector_t cur_sector = thread_current()->cur_dir_sector;
   // printf("welcome! cur_sector must be 1\n",cur_sector);
    //printf("name %s\n", arg_tokens[0]);

    struct dir *dir = dir_open(inode_open(cur_sector)); //open something directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, arg_tokens[0], &inode)==false) //on current directory : couldn't find : able to create
    {
        dir_close(dir);
        result->valid = true;
        result->dir_inode = inode_open(cur_sector); //current directory
        result->is_dir = must_dir;
        result->filename = arg_tokens[0];
        //memcpy("thisone??? name still %s and its length is %d \n", arg_tokens[0], strlen(arg_tokens[0]));
        //strlcpy(result->filename, arg_tokens[0], strlen(arg_tokens[0])+1);
        //for (int i=0; i<strlen(arg_tokens[0])+1; i++)
        //{
          //result->filename[i] = (*arg_tokens)[i];
        //}
        //printf("result filename %s\n", result->filename);
        free(arg_tokens);
        //printf("result filename %s\n", result->filename);
        return result;
    }
    else /// if a coulud find it : not able to create
    {
      dir_close(dir);
      inode_close(inode);
      result->valid = false;
      free(arg_tokens);
      return result;
    }
  }
  //////////////////////////////////////NOW argc>=2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]==".") // case (1) : cd ./something
        {
          result->dir_inode = inode_open(thread_current()->cur_dir_sector); //parent directory : current directory
          result->valid = true;
        }
        else if(arg_tokens[i]!="..")  //case (2) cd a/something
        {
          struct dir *dir = dir_open(inode_open(thread_current()->cur_dir_sector)); //parent directory : current directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at parent
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
            }
          }
          else /// if a couldn't find at parent directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
        else //case (3) cd ../something
        {
          printf("cur sector is 1 right? %d\n",thread_current()->cur_dir_sector);
          struct inode *inode = inode_open(thread_current()->cur_dir_sector);
          result->valid = true;
          if(inode->sector == 1)
          {
            result->dir_inode = inode;
          }
          else
          {
            result->dir_inode = inode_open(inode->prev_sector); //parent directory : parent of current directory;
            inode_close(inode);
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_create(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_create(result, arg_tokens[i], i, argc-1, must_dir);
      }
    }
  //////////////done
  free(arg_tokens);
  return result;
}

static struct parsing_result *update_result_relative_create(
                struct parsing_result *result, char* name,int start, int end) //start : 무조건 2번째 index(1) 이상
{
  if(result->valid ==false) return result;
  if(start!=end) //끝이 아니라면
  {
    if(name==".") //case(1) something A/./something B
    {
      return result;
    }
    if(name=="..") //case(2) something A/../something B
    {
      if(result->dir_inode->sector == 1) return result; //if something A == root directory
      //if something A != root directory
      struct inode *inode;
      inode = inode_open(result->dir_inode->prev_sector); //previous directory inode open
      inode_close(result->dir_inode); //current directory inode close
      if(inode->is_dir==false) PANIC("PARENT DIRECTORY's inode isn't directory"); //and previsou directory must directory
      result->dir_inode = inode;
      return result;
    }
  }
  if(start==end) //case(1) : something/. or case(2) : something/.. : cannot create
  {
    inode_close(result->dir_inode);
    result->valid = false;
    return result;
  }
}

static struct parsing_result *update_result_absolute_create(
                struct parsing_result *result, char* name,int start, int end, bool must_dir) //start : 무조건 2번째 index(1) 이상
//name isn't . or ..
{
  if(result->valid ==false) return result;
  if(start!=end) //끝이 아니라면 ex : cd  soemthing A/name/something B즉 name must be directory
  {
    struct dir *dir = dir_open(result->dir_inode); //open something A directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, name, &inode)==true) // find name success
    {
      if(inode->is_dir==false) //but it is not directory
      {
        result->valid = false;
        dir_close(dir);
        inode_close(inode);
        return result;
      }
      else //and it is directory
      {
        result->valid = true;
        result->dir_inode = inode;
        dir_close(dir);
        return result;
      }
    }
    else //find name success fail
    {
      dir_close(dir);
      result->valid = false;
      return result;
    }   
  }
  ////////////////////////////// start == end : ex) cd something/name
  struct dir *dir = dir_open(result->dir_inode); //open something directory
  struct inode *inode = NULL;
  if(dir_lookup(dir, name, &inode)==false) //find name fail : can create
  {
    block_sector_t parent_dir_inode_number = result->dir_inode->sector; //something directory inode number
    result->valid = true;
    result->filename = name;
    dir_close(dir);
    result->is_dir = must_dir;
    result->dir_inode = inode_open(parent_dir_inode_number);
    return result;
  }
  else //find name success : cannot create
  {
    dir_close(dir);
    inode_close(inode);
    result->valid = false;
    return result;
  }
}

//////////////////////////////



struct parsing_result *parse_mkdir(char *filename)
{
  //printf("filename is %s\n",filename);
  struct parsing_result *result = malloc(sizeof(struct parsing_result));
  result->valid = false;
  result->is_dir = false;
  result->is_root = false;
  // filename NULL : false
  if(filename==NULL||strlen(filename)==0)
  {
    result->valid = false;
    return result;
  }
 // printf("parse_create enter\n");
  //printf("filename %s\n", filename);
  // 젤 마지막 문자가 / 일 경우, ex : cd a/b/ -> parsing 하면 a 와 b 로 나누어 질 것임. 이 경우 b는 무조건 directory여야 함
  // 이걸 체크하기 위한 flag -> create에서는 return->is_dir flag를 설정하기 위해서 확인
  //mkdir 에서는 확인할 필요가 없음 ㅇㅇ


  //크게 케이스 4가지 
  //절대경로 : 첫 문자가 (1) /
  //상대경로 (2) .. (3) ./a  == (4) a
  bool absolute ;
  if(filename[0]=='/') absolute = true;
  else absolute = false;
  //printf("must_dir %d absolute %d\n", must_dir,absolute);
  ////////////////////////////////////////////////////parsing
  int num=8;
  char **arg_tokens = malloc(num); //it must free
  int argc=0;
  char *token, *save_ptr;

  for(token = strtok_r(filename, "/", &save_ptr); token !=NULL; token = strtok_r (NULL, "/", &save_ptr))
  {
    if(argc*8>=num)
    {
      num=num*2; 
      arg_tokens = realloc(arg_tokens, num);
    }
    arg_tokens[argc]=token;
    argc++;
  }
  arg_tokens[argc]='\0';
  //arg_tokens : parsing 넣어둔거, argc : 길이
  ////////////////////////////////////////////////////parsing done
  // 특수 케이스 : argc 가 0일 때 :: 즉 cd /////// 이런 거. : root directory create
  if(argc == 0)
  {
    result->valid = true;
    result->is_root = true;
    result->is_dir = true;
    free(arg_tokens);
    return result;
  }
  ///////////////////////////////////////절대경로 case (1)
  if(absolute)
  {
    if(argc==1) // 이경우 case(1-1) cd ///.., case(1-2) cd ///., case(1-3) cd ///a
    {
      if(arg_tokens[0]=="." || arg_tokens[0]=="..") //case (1-1), case (1-2) : root directory create
      {
        result->valid = true;
        result->is_root = true;
        result->is_dir = true;
        free(arg_tokens);
        return result;
      }
      else //case (1-3)
      { 
        struct dir *dir = dir_open_root(); //open  root directory
        struct inode *inode = NULL;
        ////////////////////////////////////////have to check a is exist or not
        if(dir_lookup(dir,arg_tokens[0],&inode)==true) // if find it -> cannot create
        {
          dir_close(dir);
          inode_close(inode);
          result->valid  = false;
          free(arg_tokens);
          return result;
        }
        else //coudn't find it ->can create
        {
          dir_close(dir);
          result->valid = true;
          result->dir_inode = inode_open(1); //parent directory is root directory
          result->is_dir = true;
          result->filename = arg_tokens[0];
          free(arg_tokens);
          return result;
        }
      }
    }
    ///////////////////////now argc >= 2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") // case (1) : cd ////./something or cd ////../something
        {
          result->dir_inode = inode_open(1); //parent directory : root directory
          result->valid = true;
        }
        else  //case (2) cd ////a/something
        {
          struct dir *dir = dir_open_root(); //open root directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at root directory
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
              }
            }
          else /// if a couldn't find at root directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_create(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_create(result, arg_tokens[i], i, argc-1, true);
      }
    }
    free(arg_tokens);
    return result;
  }
  ////////////////////////////////NOW start with 상대경로로 시작하는 case (2) .. (3) ./a  == (4) a
  if(argc == 1) //length ==1 
  {
    //case (2) cd . case (3) cd .. case (4) cd a
    if(arg_tokens[0]=="." || arg_tokens[0]=="..") //case(2) or (3) : cannot create
    {
      result->valid = false;
      free(arg_tokens);
      return result;
    }
    /////////case (4) something/a

    block_sector_t cur_sector = thread_current()->cur_dir_sector;
   // printf("welcome! cur_sector must be 1\n",cur_sector);
    //printf("name %s\n", arg_tokens[0]);
    struct dir *dir = dir_open(inode_open(cur_sector)); //open something directory
    struct inode *inode = NULL;
    if(dir_lookup(dir, arg_tokens[0], &inode)==false) //on current directory : couldn't find : able to create
    {
        dir_close(dir);
        result->valid = true;
        result->dir_inode = inode_open(cur_sector); //current directory
        result->is_dir = true;
        result->filename = arg_tokens[0];
        //memcpy("thisone??? name still %s and its length is %d \n", arg_tokens[0], strlen(arg_tokens[0]));
        //strlcpy(result->filename, arg_tokens[0], strlen(arg_tokens[0])+1);
        //for (int i=0; i<strlen(arg_tokens[0])+1; i++)
        //{
          //result->filename[i] = (*arg_tokens)[i];
        //}
        //printf("result filename %s\n", result->filename);
        free(arg_tokens);
        //printf("result filename %s\n", result->filename);
        return result;
    }
    else /// if a coulud find it : not able to create
    {
      dir_close(dir);
      inode_close(inode);
      result->valid = false;
      free(arg_tokens);
      return result;
    }
  }
  //////////////////////////////////////NOW argc>=2
    for(int i=0; i<argc; i++)
    {
      if(i==0) //first index 처리
      {
        if(arg_tokens[i]==".") // case (1) : cd ./something
        {
          result->dir_inode = inode_open(thread_current()->cur_dir_sector); //parent directory : current directory
          result->valid = true;
        }
        else if(arg_tokens[i]!="..")  //case (2) cd a/something
        {
          struct dir *dir = dir_open(inode_open(thread_current()->cur_dir_sector)); //parent directory : current directory
          struct inode *inode = NULL;

          if(dir_lookup(dir, arg_tokens[i], &inode)==true) //a must exists at parent
          {
            if(inode->is_dir==false) //
            {
              result->valid = false;
              dir_close(dir);
              inode_close(inode);
            }
            else //and a must be directory
            {
              result->valid = true;
              dir_close(dir);
              result->dir_inode = inode; 
            }
          }
          else /// if a couldn't find at parent directory
          {
            dir_close(dir);
            result->valid = false;
          }
        }
        else //case (3) cd ../something
        {
          struct inode *inode = inode_open(thread_current()->cur_dir_sector);
          result->valid = true;
          if(inode->sector == 1)
          {
            result->dir_inode = inode;
          }
          else
          {
            result->dir_inode = inode_open(inode->prev_sector); //parent directory : parent of current directory;
            inode_close(inode);
          }
        }
      }
      else ////////////////////////그다음 index들 처리
      {
        if(arg_tokens[i]=="." || arg_tokens[i]=="..") result = update_result_relative_create(result, arg_tokens[i], i, argc-1);
        else result = update_result_absolute_create(result, arg_tokens[i], i, argc-1, true);
      }
    }
  //////////////done
  free(arg_tokens);
  return result;
}

