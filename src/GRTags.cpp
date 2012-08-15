#include "GRTags.h"
#include "GRRecurseJob.h"
#include "Server.h"
#include "Indexer.h"
#include "GRParseJob.h"
#include <math.h>

GRTags::GRTags()
    : mWatcher(new FileSystemWatcher), mCount(0), mActive(0)
{
    mWatcher->modified().connect(this, &GRTags::onDirectoryModified);
}

void GRTags::init(const shared_ptr<Project> &proj)
{
    mProject = proj;
    mSrcRoot = proj->srcRoot;
    assert(mSrcRoot.endsWith('/'));

    ScopedDB database = proj->db(Project::GRFiles, ReadWriteLock::Write);
    RTags::Ptr<Iterator> it(database->createIterator());
    it->seekToFirst();
    {
        Batch batch(database);
        while (it->isValid()) {
            const ByteArray fileName = it->key().byteArray();
            const Path path = mSrcRoot + fileName;
            if (!path.exists()) {
                batch.remove(it->key());
            } else {
                const time_t value = it->value<time_t>();
                const time_t modified = path.lastModified();
                const Path dir = path.parentDir();
                Map<ByteArray, time_t> &files = mFiles[dir];
                files[fileName] = value;
                if (files.size() == 1)
                    mWatcher->watch(dir);
                if (modified > value) {
                    parse(path, GRParseJob::Dirty);
                }
            }
            it->next();
        }
    }
    recurseDirs();
}

void GRTags::recurseDirs()
{
    GRRecurseJob *job = new GRRecurseJob(mSrcRoot);
    job->finished().connect(this, &GRTags::onRecurseJobFinished);
    Server::instance()->threadPool()->start(job);
}

void GRTags::onRecurseJobFinished(Map<Path, bool> &paths)
{
    shared_ptr<Project> project = mProject.lock();
    ScopedDB database = project->db(Project::GRFiles, ReadWriteLock::Write);
    RTags::Ptr<Iterator> it(database->createIterator());
    it->seekToFirst();
    Path p = mSrcRoot;
    p.reserve(PATH_MAX);
    while (it->isValid()) {
        const Slice slice = it->key();
        p.append(slice.data(), slice.size());
        const Map<Path, bool>::iterator found = paths.find(p);
        if (found == paths.end()) { // file is removed
            remove(p, &database);
        } else {
            paths.erase(found);
        }
        p.resize(mSrcRoot.size());
        it->next();
    }
    for (Map<Path, bool>::const_iterator i = paths.begin(); i != paths.end(); ++i) {
        if (i->second) {
            parse(i->first, GRParseJob::None); // not dirty
        } else {
            Map<ByteArray, time_t> &files = mFiles[i->first.parentDir()];
            files[i->first.fileName()] = 0;
        }
    }
}

void GRTags::onParseJobFinished(GRParseJob *job, const Map<ByteArray, Map<Location, bool> > &entries)
{
    {
        MutexLocker lock(&mMutex);
        --mActive;
        const int idx = mCount - mActive;
        error("[%3d%%] Tagged %s %d/%d. %d entries.",
              static_cast<int>(round((static_cast<double>(idx) / static_cast<double>(mCount)) * 100.0)),
              job->path().constData(), idx, mCount, entries.size());
        if (mActive == 0)
            mCount = 0;
    }

    const Path &file = job->path();
    const time_t parseTime = job->parseTime();
    if (file.lastModified() > parseTime) { // already outdated
        parse(file, job->flags());
        return;
    }
    const Path dir = file.parentDir();
    const Slice fileName(file.constData() + mSrcRoot.size(), file.size() - mSrcRoot.size());
    shared_ptr<Project> project = mProject.lock();
    ScopedDB database = project->db(Project::GRFiles, ReadWriteLock::Write);
    Map<ByteArray, time_t> &files = mFiles[dir];
    files[fileName.byteArray()] = parseTime;
    if (files.size() == 1)
        mWatcher->watch(dir);
    database->setValue(fileName, parseTime);
    database = project->db(Project::GR, ReadWriteLock::Write);
    if (job->flags() & GRParseJob::Dirty) {
        dirty(Location::fileId(file), database);
    }

    // for the Dirty case we could do it in one pass instead of two
    Batch batch(database);
    for (Map<ByteArray, Map<Location, bool> >::const_iterator it = entries.begin(); it != entries.end(); ++it) {
        const Map<Location, bool> val = it->second;
        Map<Location, bool> old = database->value<Map<Location, bool> >(it->first);
        for (Map<Location, bool>::const_iterator i = val.begin(); i != val.end(); ++i) {
            old[i->first] = i->second;
        }

        batch.add(it->first, old);
    }
}

void GRTags::onDirectoryModified(const Path &path)
{
    Map<ByteArray, time_t> &files = mFiles[path];
    Path p(path);
    p.resize(PATH_MAX); // this is okay because stat(2) is called with a null terminated string, not Path::size()
    char *dest = p.data() + path.size();
    Map<ByteArray, time_t>::iterator it = files.begin();
    while (it != files.end()) {
        // ### we need to do another recursejob when files are added/removed though
        const ByteArray &key = it->first;
        if (key.size() < PATH_MAX - path.size()) {
            strncpy(dest, key.nullTerminated(), key.size() + 1);
            const time_t lastModified = p.lastModified(); // 0 means failed to stat so probably removed
            if (!lastModified) {
                remove(p);
                files.erase(it++);
                continue;
            }
            if (it->second && lastModified > it->second) {
                parse(path, GRParseJob::Dirty);
            }
        }
        ++it;
    }

    if (files.isEmpty()) {
        mWatcher->unwatch(path);
        mFiles.remove(path);
    }
}

void GRTags::remove(const Path &file, ScopedDB *grfiles)
{
    shared_ptr<Project> project = mProject.lock();
    ScopedDB database = (grfiles ? *grfiles : project->db(Project::GRFiles, ReadWriteLock::Write));
    RTags::Ptr<Iterator> it(database->createIterator());
    const Slice key(file.constData() + mSrcRoot.size(), file.size() - mSrcRoot.size());
    database->remove(key);
    database.reset();
    database = project->db(Project::GR, ReadWriteLock::Write);
    dirty(Location::fileId(file), database);
}

void GRTags::dirty(uint32_t fileId, ScopedDB &db)
{
    Batch batch(db);
    RTags::Ptr<Iterator> it(db->createIterator());
    it->seekToFirst();
    while (it->isValid()) {
        Map<Location, bool> val = it->value<Map<Location, bool> >();
        Map<Location, bool>::iterator i = val.begin();
        bool changed = false;
        while (i != val.end()) {
            if (i->first.fileId() == fileId) {
                val.erase(i++);
                changed = true;
            } else {
                ++i;
            }
        }
        if (changed) {
            if (val.isEmpty()) {
                batch.remove(it->key());
            } else {
                batch.add(it->key(), val);
            }
        }
        it->next();
    }
}
void GRTags::parse(const Path &path, unsigned flags)
{
    {
        MutexLocker lock(&mMutex);
        ++mCount;
        ++mActive;
    }
    GRParseJob *job = new GRParseJob(path, flags);
    job->finished().connect(this, &GRTags::onParseJobFinished);
    Server::instance()->threadPool()->start(job);
}