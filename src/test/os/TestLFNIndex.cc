// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Cloudwatt <libre.licensing@cloudwatt.com>
 *
 * Author: Loic Dachary <loic@dachary.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library Public License for more details.
 *
 */

#include <stdio.h>
#include <signal.h>
#include "os/LFNIndex.h"
#include "os/chain_xattr.h"
#include "common/ceph_argparse.h"
#include "global/global_init.h"
#include <gtest/gtest.h>

class TestWrapLFNIndex : public LFNIndex {
public:
  TestWrapLFNIndex(coll_t collection,
		   const char *base_path,
		   uint32_t index_version) : LFNIndex(collection, base_path, index_version) {}

  virtual uint32_t collection_version() {
    return index_version;
  }

  int cleanup() { return 0; }

  virtual int _split(
		     uint32_t match,                           
		     uint32_t bits,                            
		     std::tr1::shared_ptr<CollectionIndex> dest
		     ) { return 0; }

  void test_generate_and_parse(const hobject_t &hoid, const std::string &mangled_expected) {
    const std::string mangled_name = lfn_generate_object_name(hoid);
    EXPECT_EQ(mangled_expected, mangled_name);
    hobject_t hoid_parsed;
    EXPECT_TRUE(lfn_parse_object_name(mangled_name, &hoid_parsed));
    EXPECT_EQ(hoid, hoid_parsed);
  }

protected:
  virtual int _init() { return 0; }

  virtual int _created(
		       const vector<string> &path,
		       const hobject_t &hoid,     
		       const string &mangled_name 
		       ) { return 0; }

  virtual int _remove(
		      const vector<string> &path,
		      const hobject_t &hoid,    
		      const string &mangled_name
		      ) { return 0; }

  virtual int _lookup(
		      const hobject_t &hoid,
		      vector<string> *path,
		      string *mangled_name,
		      int *exists		 
		      ) { return 0; }

  virtual int _collection_list(
			       vector<hobject_t> *ls
			       ) { return 0; }

  virtual int _collection_list_partial(
				       const hobject_t &start,
				       int min_count,
				       int max_count,
				       snapid_t seq,
				       vector<hobject_t> *ls,
				       hobject_t *next
				       ) { return 0; }
};

class TestHASH_INDEX_TAG : public TestWrapLFNIndex, public ::testing::Test {
public:
  TestHASH_INDEX_TAG() : TestWrapLFNIndex(coll_t("ABC"), "PATH", CollectionIndex::HASH_INDEX_TAG) {
  }
};

TEST_F(TestHASH_INDEX_TAG, generate_and_parse_name) {
  const vector<string> path;
  const std::string key;
  uint64_t hash = 0xABABABAB;
  uint64_t pool = -1;

  test_generate_and_parse(hobject_t(object_t(".A/B_\\C.D"), key, CEPH_NOSNAP, hash, pool),
			  "\\.A\\sB_\\\\C.D_head_ABABABAB");
  test_generate_and_parse(hobject_t(object_t("DIR_A"), key, CEPH_NOSNAP, hash, pool),
			  "\\dA_head_ABABABAB");
}

class TestHASH_INDEX_TAG_2 : public TestWrapLFNIndex, public ::testing::Test {
public:
  TestHASH_INDEX_TAG_2() : TestWrapLFNIndex(coll_t("ABC"), "PATH", CollectionIndex::HASH_INDEX_TAG_2) {
  }
};

TEST_F(TestHASH_INDEX_TAG_2, generate_and_parse_name) {
  const vector<string> path;
  std::string mangled_name;
  const std::string key("KEY");
  uint64_t hash = 0xABABABAB;
  uint64_t pool = -1;

  {
    std::string name(".XA/B_\\C.D");
    name[1] = '\0';
    hobject_t hoid(object_t(name), key, CEPH_NOSNAP, hash, pool);

    test_generate_and_parse(hoid, "\\.\\nA\\sB\\u\\\\C.D_KEY_head_ABABABAB");
  }
  test_generate_and_parse(hobject_t(object_t("DIR_A"), key, CEPH_NOSNAP, hash, pool),
			  "\\dA_KEY_head_ABABABAB");
}

class TestLFNIndex : public TestWrapLFNIndex, public ::testing::Test {
public:
  TestLFNIndex() : TestWrapLFNIndex(coll_t("ABC"), "PATH", CollectionIndex::HASH_INDEX_TAG) {
  }

  virtual void SetUp() {
    ::chmod("PATH", 0700);
    ::system("rm -fr PATH");
    ::mkdir("PATH", 0700);
  }

  virtual void TearDown() {
    ::system("rm -fr PATH");
  }
};

TEST_F(TestLFNIndex, remove_object) {
  const vector<string> path;

  //
  // small object name removal
  //
  {
    std::string mangled_name;
    int exists = 666;
    hobject_t hoid(sobject_t("ABC", CEPH_NOSNAP));

    EXPECT_EQ(0, ::chmod("PATH", 0000));
    EXPECT_EQ(-EACCES, remove_object(path, hoid));
    EXPECT_EQ(0, ::chmod("PATH", 0700));
    EXPECT_EQ(-ENOENT, remove_object(path, hoid));
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    const std::string pathname("PATH/" + mangled_name);
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, remove_object(path, hoid));
    EXPECT_EQ(-1, ::access(pathname.c_str(), 0));
    EXPECT_EQ(ENOENT, errno);
  }
  //
  // long object name removal of a single file
  //
  {
    std::string mangled_name;
    int exists;
    const std::string object_name(1024, 'A');
    hobject_t hoid(sobject_t(object_name, CEPH_NOSNAP));

    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_EQ(0, exists);
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    std::string pathname("PATH/" + mangled_name);
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, created(hoid, pathname.c_str()));

    EXPECT_EQ(0, remove_object(path, hoid));
    EXPECT_EQ(-1, ::access(pathname.c_str(), 0));
    EXPECT_EQ(ENOENT, errno);
  }

  //
  // long object name removal of the last file 
  //
  {
    std::string mangled_name;
    int exists;
    const std::string object_name(1024, 'A');
    hobject_t hoid(sobject_t(object_name, CEPH_NOSNAP));

    //
    //   PATH/AAA..._0_long => does not match long object name
    //
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_EQ(0, exists);
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    std::string pathname("PATH/" + mangled_name);
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, created(hoid, pathname.c_str()));
    const string LFN_ATTR = "user.cephos.lfn";
    const std::string object_name_1 = object_name + "SUFFIX";
    EXPECT_EQ(object_name_1.size(), (unsigned)chain_setxattr(pathname.c_str(), LFN_ATTR.c_str(), object_name_1.c_str(), object_name_1.size()));

    //
    //   PATH/AAA..._1_long => matches long object name
    //
    std::string mangled_name_1;
    exists = 666;
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name_1, &exists));
    EXPECT_NE(std::string::npos, mangled_name_1.find("1_long"));
    EXPECT_EQ(0, exists);
    std::string pathname_1("PATH/" + mangled_name_1);
    EXPECT_EQ(0, ::close(::creat(pathname_1.c_str(), 0600)));
    EXPECT_EQ(0, created(hoid, pathname_1.c_str()));

    //
    // remove_object skips PATH/AAA..._0_long and removes PATH/AAA..._1_long
    //
    EXPECT_EQ(0, remove_object(path, hoid));
    EXPECT_EQ(0, ::access(pathname.c_str(), 0));
    EXPECT_EQ(-1, ::access(pathname_1.c_str(), 0));
    EXPECT_EQ(ENOENT, errno);
    EXPECT_EQ(0, ::unlink(pathname.c_str()));
  }

  //
  // long object name removal of a file in the middle of the list
  //
  {
    std::string mangled_name;
    int exists;
    const std::string object_name(1024, 'A');
    hobject_t hoid(sobject_t(object_name, CEPH_NOSNAP));

    //
    //   PATH/AAA..._0_long => matches long object name
    //
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_EQ(0, exists);
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    std::string pathname("PATH/" + mangled_name);
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, created(hoid, pathname.c_str()));
    //
    //   PATH/AAA..._1_long => matches long object name
    //
    std::string mangled_name_1 = mangled_name;
    mangled_name_1.replace(mangled_name_1.find("0_long"), 6, "1_long");
    const std::string pathname_1("PATH/" + mangled_name_1);
    const std::string cmd("cp --preserve=xattr " + pathname + " " + pathname_1);
    EXPECT_EQ(0, ::system(cmd.c_str()));
    const string ATTR = "user.MARK";
    EXPECT_EQ((unsigned)1, (unsigned)chain_setxattr(pathname_1.c_str(), ATTR.c_str(), "Y", 1));

    //
    // remove_object replaces the file to be removed with the last from the
    // collision list. In this case it replaces
    //    PATH/AAA..._0_long 
    // with
    //    PATH/AAA..._1_long 
    //
    EXPECT_EQ(0, remove_object(path, hoid));
    EXPECT_EQ(0, ::access(pathname.c_str(), 0));
    char buffer[1] = { 0, };
    EXPECT_EQ((unsigned)1, (unsigned)chain_getxattr(pathname.c_str(), ATTR.c_str(), buffer, 1));
    EXPECT_EQ('Y', buffer[0]);
    EXPECT_EQ(-1, ::access(pathname_1.c_str(), 0));
    EXPECT_EQ(ENOENT, errno);
  }
}

TEST_F(TestLFNIndex, get_mangled_name) {
  const vector<string> path;

  //
  // small object name
  //
  {
    std::string mangled_name;
    int exists = 666;
    hobject_t hoid(sobject_t("ABC", CEPH_NOSNAP));

    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_NE(std::string::npos, mangled_name.find("ABC_head"));
    EXPECT_EQ(std::string::npos, mangled_name.find("0_long"));
    EXPECT_EQ(0, exists);
    const std::string pathname("PATH/" + mangled_name);
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_NE(std::string::npos, mangled_name.find("ABC_head"));
    EXPECT_EQ(1, exists);
    EXPECT_EQ(0, ::unlink(pathname.c_str()));
  }
  //
  // long object name
  //
  {
    std::string mangled_name;
    int exists;
    const std::string object_name(1024, 'A');
    hobject_t hoid(sobject_t(object_name, CEPH_NOSNAP));

    //
    // long version of the mangled name and no matching
    // file exists 
    //
    mangled_name.clear();
    exists = 666;
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    EXPECT_EQ(0, exists);

    const std::string pathname("PATH/" + mangled_name);

    //
    // if a file by the same name exists but does not have the
    // expected extended attribute, it is silently removed 
    //
    mangled_name.clear();
    exists = 666;
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    EXPECT_EQ(0, exists);
    EXPECT_EQ(-1, ::access(pathname.c_str(), 0));
    EXPECT_EQ(ENOENT, errno);

    //
    // if a file by the same name exists but does not have the
    // expected extended attribute, and cannot be removed, 
    // return on error
    //
    mangled_name.clear();
    exists = 666;
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, ::chmod("PATH", 0500));
    EXPECT_EQ(-EACCES, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_EQ("", mangled_name);
    EXPECT_EQ(666, exists);
    EXPECT_EQ(0, ::chmod("PATH", 0700));
    EXPECT_EQ(0, ::unlink(pathname.c_str()));

    //
    // long version of the mangled name and a file
    // exists by that name and contains the long object name
    //
    mangled_name.clear();
    exists = 666;
    EXPECT_EQ(0, ::close(::creat(pathname.c_str(), 0600)));
    EXPECT_EQ(0, created(hoid, pathname.c_str()));
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name, &exists));
    EXPECT_NE(std::string::npos, mangled_name.find("0_long"));
    EXPECT_EQ(1, exists);
    EXPECT_EQ(0, ::access(pathname.c_str(), 0));

    //
    // long version of the mangled name and a file exists by that name
    // and contains a long object name with the same prefix but they
    // are not identical and it so happens that their SHA1 is
    // identical : a collision number is used to differentiate them
    //
    const string LFN_ATTR = "user.cephos.lfn";
    const std::string object_name_same_prefix = object_name + "SUFFIX";
    EXPECT_EQ(object_name_same_prefix.size(), (unsigned)chain_setxattr(pathname.c_str(), LFN_ATTR.c_str(), object_name_same_prefix.c_str(), object_name_same_prefix.size()));
    std::string mangled_name_same_prefix;
    exists = 666;
    EXPECT_EQ(0, get_mangled_name(path, hoid, &mangled_name_same_prefix, &exists));
    EXPECT_NE(std::string::npos, mangled_name_same_prefix.find("1_long"));
    EXPECT_EQ(0, exists);
    
    EXPECT_EQ(0, ::unlink(pathname.c_str()));
  }
}

int main(int argc, char **argv) {
  int fd = ::creat("detect", 0600);
  int ret = chain_fsetxattr(fd, "user.test", "A", 1);
  ::close(fd);
  ::unlink("detect");
  if (ret < 0) {
    cerr << "SKIP LFNIndex because unable to test for xattr" << std::endl;
  } else {
    vector<const char*> args;
    argv_to_vec(argc, (const char **)argv, args);

    global_init(NULL, args, CEPH_ENTITY_TYPE_CLIENT, CODE_ENVIRONMENT_UTILITY, 0);
    common_init_finish(g_ceph_context);

    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
  }
}

// Local Variables:
// compile-command: "cd ../.. ; make unittest_lfnindex ; valgrind --tool=memcheck ./unittest_lfnindex # --gtest_filter=TestLFNIndex.* --log-to-stderr=true --debug-filestore=20"
// End:
