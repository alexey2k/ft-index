// No include header guard as this file is meant to be included exactly once.
// A second include should mean something's wrong

#include <mysql/psi/mysql_file.h> // PSI_file
#include <mysql/psi/mysql_thread.h> // PSI_mutex

#ifndef HAVE_PSI_MUTEX_INTERFACE
#error HAVE_PSI_MUTEX_INTERFACE required
#endif
#ifndef HAVE_PSI_RWLOCK_INTERFACE
#error HAVE_PSI_RWLOCK_INTERFACE required
#endif
#ifndef HAVE_PSI_THREAD_INTERFACE
#error HAVE_PSI_THREAD_INTERFACE required
#endif

#define toku_likely(x)   __builtin_expect(!!(x),1)
#define toku_unlikely(x) __builtin_expect(!!(x),0)

//#if TOKU_THREAD_DEBUG
extern int mutex_all_stat_init[300];
extern int mutex_all_stat[300];
//#endif

// Instrumentation keys

class toku_instr_key
{
private:
    pfs_key_t   m_id;
public:
    toku_instr_key(toku_instr_object_type type,
                   const char *group,
                   const char *name)
    {
        switch (type)
        {
        case toku_instr_object_type::mutex:
            {
              PSI_mutex_info mutex_info{&m_id, name, 0};
              mysql_mutex_register(group, &mutex_info, 1);
            }
            break;
        case toku_instr_object_type::rwlock:
            {
              PSI_rwlock_info rwlock_info{&m_id, name, 0};
              mysql_rwlock_register(group, &rwlock_info, 1);
            }
            break;
        case toku_instr_object_type::cond:
            {
              PSI_cond_info cond_info{&m_id, name, 0};
              mysql_cond_register(group, &cond_info, 1);
            }
            break;
        case toku_instr_object_type::thread:
            {
              PSI_thread_info thread_info{&m_id, name, 0};
              mysql_thread_register(group, &thread_info, 1);
            }
            break;
        case toku_instr_object_type::file:
            {
              PSI_file_info file_info{&m_id, name, 0};
              mysql_file_register(group, &file_info, 1);
            }
            break;
        }
    }

    explicit toku_instr_key(pfs_key_t key_id) : m_id(key_id) { }

    pfs_key_t id() const { return m_id; }
};



// Thread instrumentation

inline
int toku_pthread_create(const toku_instr_key &key, pthread_t *thread,
                        const pthread_attr_t *attr,
                        void *(*start_routine)(void*), void *arg)
{
#if TOKU_THREAD_DEBUG
    fprintf(stderr, "-------------------------------------------------Registering toku thread: key: %d\n",key.id());
#endif    
    return PSI_THREAD_CALL(spawn_thread)(key.id(), thread, attr, start_routine, arg);
}

inline
void toku_instr_register_current_thread(const toku_instr_key &key)
{
#if TOKU_THREAD_DEBUG
    fprintf(stderr, "Registering toku thread: key: %d\n",key.id());
#endif    
    
   struct PSI_thread* psi_thread = PSI_THREAD_CALL(new_thread)
                                                  (key.id(), NULL, 0);
   PSI_THREAD_CALL(set_thread)(psi_thread);                        
}


inline
void toku_instr_delete_current_thread()
{
#if TOKU_THREAD_DEBUG
    fprintf(stderr, "De-registering toku thread: key: %d\n",key.id());
#endif    
    PSI_THREAD_CALL(delete_current_thread)();
}

// I/O instrumentation

enum class toku_instr_file_op {
    file_stream_open = PSI_FILE_STREAM_OPEN,
    file_create = PSI_FILE_CREATE,
    file_open = PSI_FILE_OPEN,
    file_delete = PSI_FILE_DELETE,
    file_read = PSI_FILE_READ,
    file_write = PSI_FILE_WRITE,
    file_sync = PSI_FILE_SYNC,
    file_stream_close = PSI_FILE_STREAM_CLOSE,
    file_close = PSI_FILE_CLOSE,
    file_stat = PSI_FILE_STAT,    
};

struct toku_io_instrumentation
{
    struct PSI_file_locker *locker;
    PSI_file_locker_state state;

    toku_io_instrumentation() : locker(nullptr) { }
};

inline
void toku_instr_file_open_begin(toku_io_instrumentation &io_instr,
                                const toku_instr_key &key,
                                toku_instr_file_op op,
                                const char *name, const char *src_file,
                                int src_line)
{
    io_instr.locker = PSI_FILE_CALL(get_thread_file_name_locker)
        (&io_instr.state, key.id(),(PSI_file_operation) op, name, io_instr.locker);
    if (io_instr.locker != NULL) {
#if TOKU_THREAD_DEBUG
        fprintf(stderr,"FILE: registering file: start_file_open_wait: op: %d\n", (PSI_file_operation) op);
#endif
        PSI_FILE_CALL(start_file_open_wait)(io_instr.locker,
                                            src_file, src_line);
    }
#if TOKU_THREAD_DEBUG
    else{
        fprintf(stderr,"FILE: Can't registering file: start_file_open_wait\n");
    }
#endif    
}

inline
void toku_instr_file_stream_open_end(toku_io_instrumentation &io_instr, TOKU_FILE &file)
{
    file.key = nullptr;
    if (toku_likely(io_instr.locker))
    {
        file.key = PSI_FILE_CALL(end_file_open_wait)(io_instr.locker,
                                                     file.file);
#if TOKU_THREAD_DEBUG
        fprintf(stderr,"FILE: registering file: %p with key %p\n",file.file, file.key);
#endif
    }
#if TOKU_THREAD_DEBUG
    else
    {
           fprintf(stderr,"FILE: Can't registering stream open end file locker NULL\n");
    }
#endif    
}

inline
void toku_instr_file_open_end(toku_io_instrumentation &io_instr, int fd) 
{
    if (toku_likely(io_instr.locker))
        PSI_FILE_CALL(end_file_open_wait_and_bind_to_descriptor)
                            (io_instr.locker, fd);
}

inline
void toku_instr_file_name_close_begin(toku_io_instrumentation &io_instr,
                                const toku_instr_key &key,
                                toku_instr_file_op op,
                                const char *name, const char *src_file,
                                int src_line)
{
    io_instr.locker = PSI_FILE_CALL(get_thread_file_name_locker)
        (&io_instr.state, key.id(),(PSI_file_operation) op, name, io_instr.locker); 
    if (toku_likely(io_instr.locker)) {
        PSI_FILE_CALL(start_file_close_wait)(io_instr.locker,
                                            src_file, src_line);
    }
}

inline
void toku_instr_file_stream_close_begin(toku_io_instrumentation &io_instr,
                                toku_instr_file_op op, TOKU_FILE &file,
                                const char *src_file, int src_line)
{
    io_instr.locker = nullptr;
    if (toku_likely(file.key))
    {
      io_instr.locker = PSI_FILE_CALL(get_thread_file_stream_locker)
          (&io_instr.state, file.key, (PSI_file_operation) op);
      if (toku_likely(io_instr.locker)) {
          PSI_FILE_CALL(start_file_close_wait)(io_instr.locker,
                                            src_file, src_line);
      }
    }
}

inline
void toku_instr_file_fd_close_begin(toku_io_instrumentation &io_instr,
                                toku_instr_file_op op, int fd, 
                                const char *src_file, int src_line)
{
    io_instr.locker = PSI_FILE_CALL(get_thread_file_descriptor_locker)
        (&io_instr.state, fd, (PSI_file_operation) op); 
    if (toku_likely(io_instr.locker)) {
        PSI_FILE_CALL(start_file_close_wait)(io_instr.locker,
                                            src_file, src_line);
    }
}

inline
void toku_instr_file_close_end(toku_io_instrumentation &io_instr, int result) 
{
    if (toku_likely(io_instr.locker))
        PSI_FILE_CALL(end_file_close_wait)
                            (io_instr.locker, result);
}

inline
void toku_instr_file_io_begin(toku_io_instrumentation &io_instr,
                                toku_instr_file_op op, int fd, 
                                ssize_t count,
                                const char *src_file, int src_line)
{

    io_instr.locker = PSI_FILE_CALL(get_thread_file_descriptor_locker)
        (&io_instr.state, fd, (PSI_file_operation) op); 
    if (toku_likely(io_instr.locker)) {
        PSI_FILE_CALL(start_file_wait)(io_instr.locker, count,
                                            src_file, src_line);
    }
}

inline
void toku_instr_file_name_io_begin(toku_io_instrumentation &io_instr,
                                const toku_instr_key &key,
                                toku_instr_file_op op,
                                const char *name,
                                ssize_t count,
                                const char *src_file, int src_line)
{
    io_instr.locker = PSI_FILE_CALL(get_thread_file_name_locker)
        (&io_instr.state, key.id(),(PSI_file_operation) op, name, &io_instr.locker); 
    if (toku_likely(io_instr.locker)) {
        PSI_FILE_CALL(start_file_wait)(io_instr.locker, count,
                                            src_file, src_line);
    }
}

inline
void toku_instr_file_stream_io_begin(toku_io_instrumentation &io_instr,
                                toku_instr_file_op op, TOKU_FILE &file,
                                ssize_t count,
                                const char *src_file, int src_line)
{
    io_instr.locker = nullptr;
    if (toku_likely(file.key))
    {
       io_instr.locker = PSI_FILE_CALL(get_thread_file_stream_locker)
        (&io_instr.state, file.key, (PSI_file_operation) op); 
       if (toku_likely(io_instr.locker)) {
        PSI_FILE_CALL(start_file_wait)(io_instr.locker, count,
                                            src_file, src_line);
       }
    }
}

inline
void toku_instr_file_io_end(toku_io_instrumentation &io_instr, 
                            ssize_t count) 
{
    if (toku_likely(io_instr.locker))
        PSI_FILE_CALL(end_file_wait)
                            (io_instr.locker, count);
}


// Mutex instrumentation

struct toku_mutex_instrumentation
{
    struct PSI_mutex_locker *locker;
    PSI_mutex_locker_state state;

    toku_mutex_instrumentation() : locker(nullptr) { }
};

inline
void toku_instr_mutex_init(const toku_instr_key &key,
                                 toku_mutex_t &mutex)
{
    mutex.psi_mutex = PSI_MUTEX_CALL(init_mutex)(key.id(), &mutex.pmutex);
#if TOKU_PTHREAD_DEBUG
    mutex.instr_key_id = key.id();
    if (key.id() != PFS_NOT_INSTRUMENTED && mutex.psi_mutex == NULL )
        fprintf(stderr,"initing tokudb mutex: key: %d NULL\n", key.id());
//    else
//    if (key.id() != PFS_NOT_INSTRUMENTED && mutex.psi_mutex != NULL )
//        fprintf(stderr,"initing tokudb mutex: key: %d OK\n", key.id());
    if (key.id() != PFS_NOT_INSTRUMENTED)
        mutex_all_stat_init[mutex.instr_key_id]++;
#endif

}

inline
void toku_instr_mutex_destroy(PSI_mutex* &mutex_instr)
{
    if (mutex_instr != nullptr)
    {
        PSI_MUTEX_CALL(destroy_mutex)(mutex_instr);
        mutex_instr = nullptr;
    }
}

inline
void toku_instr_mutex_lock_start(toku_mutex_instrumentation &mutex_instr,
                                 toku_mutex_t &mutex,
                                 const char *src_file, int src_line)
{
    mutex_instr.locker=nullptr;
    if (mutex.psi_mutex)
    {
        mutex_instr.locker = PSI_MUTEX_CALL(start_mutex_wait)
            (&mutex_instr.state, mutex.psi_mutex, PSI_MUTEX_LOCK,
             src_file, src_line);
#if TOKU_PTHREAD_DEBUG
        if (!mutex_instr.locker)
            fprintf(stderr, "Can't start timer for mutex key: %d: file %s: line: %d\n",
                    mutex.instr_key_id, src_file, src_line);
#endif
    }
#if TOKU_PTHREAD_DEBUG
    if (mutex.instr_key_id != PFS_NOT_INSTRUMENTED)
    mutex_all_stat[mutex.instr_key_id]++;
#endif
}

inline
void toku_instr_mutex_trylock_start(toku_mutex_instrumentation &mutex_instr,
                                    toku_mutex_t &mutex,
                                    const char *src_file, int src_line)
{
    mutex_instr.locker=nullptr;
    if (mutex.psi_mutex)
        {
            mutex_instr.locker = PSI_MUTEX_CALL(start_mutex_wait)
                (&mutex_instr.state, mutex.psi_mutex, PSI_MUTEX_TRYLOCK,
                 src_file, src_line);
        }
#if TOKU_PTHREAD_DEBUG
    if (mutex.instr_key_id != PFS_NOT_INSTRUMENTED)
    mutex_all_stat[mutex.instr_key_id]++;
#endif
}

inline
void toku_instr_mutex_lock_end(toku_mutex_instrumentation &mutex_instr,
                               int pthread_mutex_lock_result)
{
    if (mutex_instr.locker)
        PSI_MUTEX_CALL(end_mutex_wait)(mutex_instr.locker,
                                       pthread_mutex_lock_result);
}

inline
void toku_instr_mutex_unlock(PSI_mutex *mutex_instr)
{
    if (mutex_instr)
        PSI_MUTEX_CALL(unlock_mutex)(mutex_instr);
}

// Instrumentation probes

class toku_instr_probe_pfs
{
 private:
    toku_mutex_t mutex;
    toku_mutex_instrumentation mutex_instr;
 public:
    explicit toku_instr_probe_pfs(const toku_instr_key &key)
    {
        toku_mutex_init(key, &mutex, nullptr);
    }

    ~toku_instr_probe_pfs()
    {
        toku_mutex_destroy(&mutex);
    }

    void start_with_source_location(const char *src_file, int src_line)
    {
        mutex_instr.locker = nullptr;
        toku_instr_mutex_lock_start(mutex_instr, mutex, src_file, src_line);
    }

    void stop()
    {
        toku_instr_mutex_lock_end(mutex_instr, 0);
    }
};

typedef toku_instr_probe_pfs toku_instr_probe;


// Condvar instrumentation

struct toku_cond_instrumentation
{
    struct PSI_cond_locker *locker;
    PSI_cond_locker_state state;

    toku_cond_instrumentation() : locker(nullptr) { }
};

enum class toku_instr_cond_op {
    cond_wait = PSI_COND_WAIT,
    cond_timedwait = PSI_COND_TIMEDWAIT,
};

inline
void toku_instr_cond_init(const toku_instr_key &key,
                                 toku_cond_t &cond)
{
    cond.psi_cond = PSI_COND_CALL(init_cond)(key.id(), &cond.pcond);
#if TOKU_PTHREAD_DEBUG
    cond.instr_key_id = key.id();
    if (key.id() != PFS_NOT_INSTRUMENTED && cond.psi_cond == NULL )
        fprintf(stderr,"initing tokudb cond_var: key: %d NULL\n", key.id());
#endif
}

inline
void toku_instr_cond_destroy(PSI_cond* &cond_instr)
{
    if (cond_instr != nullptr)
    {
        PSI_COND_CALL(destroy_cond)(cond_instr);
        cond_instr = nullptr;
    }
}

inline
void toku_instr_cond_wait_start(toku_cond_instrumentation &cond_instr,
                                toku_instr_cond_op op,
                                toku_cond_t &cond, toku_mutex_t &mutex,
                                const char *src_file, int src_line)
{
    cond_instr.locker= nullptr;
    if (cond.psi_cond)
    {
    /* Instrumentation start */
        cond_instr.locker= PSI_COND_CALL(start_cond_wait)
            (&cond_instr.state, cond.psi_cond, mutex.psi_mutex,
             (PSI_cond_operation) op, src_file, src_line);
#if TOKU_PTHREAD_DEBUG
        if (!cond_instr.locker)
            fprintf(stderr, "Can't start timer for cond_var key: %d\n",
                    cond.instr_key_id);
#endif
    }

}

inline
void toku_instr_cond_wait_end(toku_cond_instrumentation &cond_instr,
                               int pthread_cond_wait_result)
{
    if (cond_instr.locker)
        PSI_COND_CALL(end_cond_wait)(cond_instr.locker,
                                      pthread_cond_wait_result);
}
                               

inline
void toku_instr_cond_signal(toku_cond_t &cond)
{
    if (cond.psi_cond)
        PSI_COND_CALL(signal_cond)(cond.psi_cond);
}

inline
void toku_instr_cond_broadcast(toku_cond_t &cond)
{
    if (cond.psi_cond)
        PSI_COND_CALL(broadcast_cond)(cond.psi_cond);
}
                               
// rwlock instrumentation

struct toku_rwlock_instrumentation
{
    struct PSI_rwlock_locker *locker;
    PSI_rwlock_locker_state state;

//    toku_rwlock_instrumentation() : locker(nullptr) { }
};

inline
void toku_instr_rwlock_init(const toku_instr_key &key,
                                 toku_pthread_rwlock_t &rwlock)
{
  rwlock.psi_rwlock=PSI_RWLOCK_CALL(init_rwlock)(key.id(), &rwlock.rwlock);
#if TOKU_PTHREAD_DEBUG
  rwlock.instr_key_id = key.id();
  if (key.id() != PFS_NOT_INSTRUMENTED && rwlock.psi_rwlock == NULL )
        fprintf(stderr,"initing tokudb rwlock: key: %d NULL\n", key.id());
#endif

}

inline
void toku_instr_rwlock_destroy(PSI_rwlock* &rwlock_instr)
{
    if (rwlock_instr != nullptr)
    {
        PSI_RWLOCK_CALL(destroy_rwlock)(rwlock_instr);
        rwlock_instr = nullptr;
    }
}

inline
void toku_instr_rwlock_rdlock_wait_start(toku_rwlock_instrumentation &rwlock_instr,
                                         toku_pthread_rwlock_t &rwlock,
                                         const char *src_file, int src_line)
{
    rwlock_instr.locker=nullptr;
    if (rwlock.psi_rwlock)
    {
    /* Instrumentation start */
    rwlock_instr.locker= PSI_RWLOCK_CALL(start_rwlock_rdwait)
                         (&rwlock_instr.state, rwlock.psi_rwlock,
                          PSI_RWLOCK_READLOCK, src_file, src_line);
                                                    
#if TOKU_PTHREAD_DEBUG
        if (!rwlock_instr.locker)
            fprintf(stderr, "Can't start timer for rwlock key: %d\n",
                    rwlock.instr_key_id);
#endif
    }

}

inline
void toku_instr_rwlock_wrlock_wait_start(toku_rwlock_instrumentation &rwlock_instr,
                                         toku_pthread_rwlock_t &rwlock,
                                         const char *src_file, int src_line)
{
    rwlock_instr.locker=nullptr;
    if (rwlock.psi_rwlock)
    {
    /* Instrumentation start */
    rwlock_instr.locker= PSI_RWLOCK_CALL(start_rwlock_wrwait)
                         (&rwlock_instr.state, rwlock.psi_rwlock,
                          PSI_RWLOCK_WRITELOCK, src_file, src_line);
                                                    
#if TOKU_PTHREAD_DEBUG
        if (!rwlock_instr.locker)
            fprintf(stderr, "Can't start timer for rwlock key: %d\n",
                    rwlock.instr_key_id);
#endif
    }

}
                                              
inline
void toku_instr_rwlock_rdlock_wait_end(toku_rwlock_instrumentation &rwlock_instr,
                                       int pthread_rwlock_wait_result)
{
    if (rwlock_instr.locker)
        PSI_RWLOCK_CALL(end_rwlock_rdwait)(rwlock_instr.locker,
                                          pthread_rwlock_wait_result);
}

inline
void toku_instr_rwlock_wrlock_wait_end(toku_rwlock_instrumentation &rwlock_instr,
                                       int pthread_rwlock_wait_result)
{
    if (rwlock_instr.locker)
        PSI_RWLOCK_CALL(end_rwlock_wrwait)(rwlock_instr.locker,
                                          pthread_rwlock_wait_result);
}


inline
void toku_instr_rwlock_unlock(toku_pthread_rwlock_t &rwlock)
{
    if (rwlock.psi_rwlock)
        PSI_RWLOCK_CALL(unlock_rwlock)(rwlock.psi_rwlock);
}

                                                                 
