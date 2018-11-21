/*
 * Copyright (c) 2013 University of California, Los Angeles
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Wentao Shang <wentao@cs.ucla.edu>
 *         Qiuhan Ding <dingqiuhan@gmail.com>
 *         Zhehao Wang <wangzhehao410305@gmail.com>
 */

#include "directory.h"
#include "signature-states.h"

using namespace std;

int ndnfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  FILE_LOG(LOG_DEBUG) << "ndnfs_readdir: path=" << path << endl;
  // // read from db
  // sqlite3_stmt *stmt;
  // sqlite3_prepare_v2(db, "SELECT * FROM file_system WHERE path = ??%;", -1, &stmt, 0);
  
  // sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
  // sqlite3_bind_text(stmt, 2, "%", -1, SQLITE_STATIC);

  // cout<< "IAMHERE\n";
  // int res = sqlite3_step(stmt);
  // cout<< "EVERTHING OK!\n";
  // // {
  //   const unsigned char* dir_name = sqlite3_column_text(stmt, 0);
  //   cout<< dir_name<< endl;

  // // }

  // // Means no such dir
  // if (res == -1) {
  //   sqlite3_finalize(stmt);
  //   return -ENOENT;
  // }
  // sqlite3_finalize(stmt);


 // Read the actual dir
  DIR *dp;
  struct dirent *de;

  (void)offset;
  (void)fi;

  char fullPath[PATH_MAX];
  abs_path(fullPath, path);

  dp = opendir(fullPath);

  if (dp == NULL)
    return -errno;

  while ((de = readdir(dp)) != NULL)
  {
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = de->d_ino;
    st.st_mode = de->d_type << 12;
    if (filler(buf, de->d_name, &st, 0))
      break;
  }
  

  closedir(dp);
  return 0;
}

/*
* Anson make it work well!
* When user make a new directory;
* A new file which type is DIRECTORY(8) will be insert into db.
* But every dir have to have a parent dir;
* So I insert a root dir named "/" into file_system.
*/
int ndnfs_mkdir(const char *path, mode_t mode)
{
  FILE_LOG(LOG_DEBUG) << "ndnfs_mkdir: path=" << path << ", mode=0" << std::oct << mode << endl;
  // cout<< "Step to  ndnfs_mkdir\n";

  string dir_path, dir_name;
  split_last_component(path, dir_path, dir_name);

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(db, "SELECT * FROM file_system WHERE path = ?;", -1, &stmt, 0);
  sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
  int res = sqlite3_step(stmt);
  if (res == SQLITE_ROW)
  {
    // Cannot create file that has conflicting file name
    sqlite3_finalize(stmt);
    return -ENOENT;
  }
  sqlite3_finalize(stmt);

  // Cannot create file without creationg necessary folders
  sqlite3_prepare_v2(db, "SELECT * FROM file_system WHERE path = ?;", -1, &stmt, 0);
  sqlite3_bind_text(stmt, 1, dir_path.c_str(), -1, SQLITE_STATIC);
  res = sqlite3_step(stmt);
  if (res != SQLITE_ROW)
  {
    sqlite3_finalize(stmt);
    return -ENOENT;
  }
  sqlite3_finalize(stmt);

  // Generate first version entry for the new file
  int ver = time(0);
  sqlite3_prepare_v2(db, "INSERT INTO file_versions (path, version) VALUES (?, ?);", -1, &stmt, 0);
  sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, ver);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  // Add the file(dir is a kind of file) entry to database
  sqlite3_prepare_v2(db,
                     "INSERT INTO file_system \
                      (path, current_version, mime_type, ready_signed, type) \
                      VALUES (?, ?, ?, ?, ?);",
                     -1, &stmt, 0);
  sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, ver); // current version
  char *mime_type = "";
  sqlite3_bind_text(stmt, 3, mime_type, -1, SQLITE_STATIC); // mime_type based on ext
  enum SignatureState signatureState = NOT_READY;
  sqlite3_bind_int(stmt, 4, signatureState);
  enum FileType fileType = DIRECTORY;
  sqlite3_bind_int(stmt, 5, fileType);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  FILE_LOG(LOG_DEBUG) << "ndnfs_mkdir: Insert to database sucessful\n";

  // This is actual make directory
  char fullPath[PATH_MAX];
  abs_path(fullPath, path);
  int ret = mkdir(fullPath, mode);

  if (ret == -1)
  {
    FILE_LOG(LOG_ERROR) << "ndnfs_mkdir: mkdir failed. Errno: " << errno << endl;
    return -errno;
  }

  return 0;
}

/*
 * For rmdir, we don't need to implement recursive remove,
 * because 'rm -r' will iterate all the sub-entries (dirs or
 * files) for us and remove them one-by-one.   ---SWT
 */
int ndnfs_rmdir(const char *path)
{
  FILE_LOG(LOG_DEBUG) << "ndnfs_rmdir: path=" << path << endl;

  if (strcmp(path, "/") == 0)
  {
    // Cannot remove root dir.
    return -EINVAL;
  }

  char fullPath[PATH_MAX];
  abs_path(fullPath, path);
  int ret = rmdir(fullPath);

  if (ret == -1)
  {
    FILE_LOG(LOG_ERROR) << "ndnfs_rmdir: rmdir failed. Errno: " << errno << endl;
    return -errno;
  }

  return 0;
}