// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Red Hat
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */


#include "MDSUtility.h"
#include "include/rados/librados.hpp"

class InodeStore;

class RecoveryDriver {
  public:
    virtual int init(librados::Rados &rados, const MDSMap *mdsmap) = 0;

    /**
     * Inject an inode + dentry parents into the metadata pool,
     * based on a backtrace recovered from the data pool
     */
    virtual int inject_with_backtrace(
        const inode_backtrace_t &bt,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id) = 0;

    /**
     * Inject and inode + dentry into the lost+found directory,
     * when all wel know about a file is its inode.
     */
    virtual int inject_lost_and_found(
        inodeno_t ino,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id) = 0;

    /**
     * Create any missing roots (i.e. mydir, strays, root inode)
     */
    virtual int init_metadata(
        int64_t data_pool_id) = 0;

    /**
     * Pre-injection check that all the roots are present in
     * the metadata pool.  Used to avoid parallel workers interfering
     * with one another, by cueing the user to go run 'init' on a
     * single node before running a parallel scan.
     *
     * @param result: set to true if roots are present, else set to false
     * @returns 0 on no unexpected errors, else error code.  Missing objects
     *          are not considered an unexpected error: check *result for
     *          this case.
     */
    virtual int check_roots(bool *result) = 0;

    virtual ~RecoveryDriver() {}
};

class LocalFileDriver : public RecoveryDriver
{ 
  protected:
    const std::string path;
    librados::IoCtx &data_io;

  public:

    LocalFileDriver(const std::string &path_, librados::IoCtx &data_io_)
      : path(path_), data_io(data_io_)
    {}

    // Implement RecoveryDriver interface
    int init(librados::Rados &rados, const MDSMap *mdsmap);

    int inject_with_backtrace(
        const inode_backtrace_t &bt,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id);

    int inject_lost_and_found(
        inodeno_t ino,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id);

    int init_metadata(int64_t data_pool_id);

    int check_roots(bool *result);
};

/**
 * A class that knows how to manipulate CephFS metadata pools
 */
class MetadataDriver : public RecoveryDriver
{
  protected:

    librados::IoCtx metadata_io;

    /**
     * Create a .inode object, i.e. root or mydir
     */
    int inject_unlinked_inode(inodeno_t inono, int mode, int64_t data_pool_id);

    /**
     * Check for existence of .inode objects, before
     * trying to go ahead and inject metadata.
     */
    int root_exists(inodeno_t ino, bool *result);

    /**
     * Try and read an fnode from a dirfrag
     */
    int read_fnode(inodeno_t ino, frag_t frag, fnode_t *fnode);

    /**
     * Try and read a dentry from a dirfrag
     */
    int read_dentry(inodeno_t parent_ino, frag_t frag,
        const std::string &dname, InodeStore *inode);

    int find_or_create_dirfrag(inodeno_t ino, bool *created);

    int inject_linkage(
        inodeno_t dir_ino, const std::string &dname, const InodeStore &inode);

  public:

    // Implement RecoveryDriver interface
    int init(librados::Rados &rados, const MDSMap *mdsmap);

    int inject_with_backtrace(
        const inode_backtrace_t &bt,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id);

    int inject_lost_and_found(
        inodeno_t ino,
        uint64_t size,
        time_t mtime,
        uint32_t chunk_size,
        int64_t data_pool_id);

    int init_metadata(int64_t data_pool_id);

    int check_roots(bool *result);
};

class DataScan : public MDSUtility
{
  protected:
    RecoveryDriver *driver;

    // IoCtx for data pool (where we scrape backtraces from)
    librados::IoCtx data_io;
    // Remember the data pool ID for use in layouts
    int64_t data_pool_id;

    uint32_t n;
    uint32_t m;

    /**
     * Scan data pool for backtraces, and inject inodes to metadata pool
     */
    int recover();

    /**
     * Scan data pool for file sizes and mtimes
     */
    int recover_extents();

  public:
    void usage();
    int main(const std::vector<const char *> &args);

    DataScan()
      : driver(NULL), data_pool_id(-1), n(0), m(1)
    {
    }

    ~DataScan()
    {
      delete driver;
    }
};

