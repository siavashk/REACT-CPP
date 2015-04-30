/**
 *  process.cpp
 *
 *  Manage an external process started in
 *  the event loop
 *
 *  @copyright 2015 Copernica B.V.
 */
#include "includes.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

/**
 *  Set up namespace
 */
namespace React {

/**
 *  Constructor
 *
 *  @param  loop        The loop that is responsible for the process
 *  @param  program     The program to execute
 *  @param  arguments   Null-terminated array of arguments
 */
Process::Process(MainLoop *loop, const char *program, const char *arguments[]) :
    _loop(loop),
    _stdin(_loop, O_CLOEXEC),
    _stdout(_loop, O_CLOEXEC),
    _stderr(_loop, O_CLOEXEC),
    _pid(fork()),
    _watcher(loop, _pid, true, [this](pid_t pid, int state) {
        
        // store result
        _state = state;

        // if we are no longer running or the program was stopped or resumed we keep monitoring
        // if result is true, then we want to continue monitoring
        bool result = _running && (WIFSTOPPED(state) || WIFCONTINUED(state));

        // make a copy of the callback
        auto callback = _callback;

        // should we stop monitoring?
        if (!result)
        {
            // indicate we are now stopped
            _running = false;

            // clear the callback
            _callback = nullptr;
        }

        // execute the callback
        if (callback) callback(state);

        // let the loop know if we want to continue
        return result;
    })
{
    // did the fork succeed?
    if (_pid < 0)
    {
        // oops, forking failed
        throw std::runtime_error(strerror(errno));
    }
    // are we running in the main process?
    else if (_pid)
    {
        // this is the main process, we are not interested in reading
        // from stdin, or writing from the other two filedescriptors
        _stdin.closeRead();
        _stdout.closeWrite();
        _stderr.closeWrite();

        // we now consider the process to be running
        _running = true;
    }
    // or the child process
    else
    {
        // use the provided pipes for stdin, stdout and stderr
        dup2(_stdin.readFd(),   STDIN_FILENO);
        dup2(_stdout.writeFd(), STDOUT_FILENO);
        dup2(_stderr.writeFd(), STDERR_FILENO);
        
        // execute the requested program (the pipe filedescriptors
        // will automatically be closed because the close-on-exec
        // bit is set for that filedescriptor
        execvp(program, const_cast<char **>(arguments));

        // this is unreachable
        _exit(127);
    }
}

/**
 *  Destructor
 */
Process::~Process()
{
    // if we are not currently running we do not have to do anything
    if (!_running) return;

    // we are now stopping
    _running = false;

    // send a term signal to the child
    kill(_pid, SIGTERM);

    // wait for the child to exit gracefully
    waitpid(_pid, nullptr, 0);
}

/**
 *  Register a handler for status changes
 *
 *  Note that if you had already registered a handler before, then that one
 *  will be reset. Only your new handler will be called when the child process
 *  changes state.
 *
 *  The callback will be invoked with a single parameter named 'status'. For
 *  more information on the meaning of this parameter, see the manual for
 *  waitpid(2) or sys/wait.h
 *
 *  @param  callback    The callback to invoke when the process changes state
 */
void Process::onStatusChange(const std::function<void(int status)> &callback)
{
    // if the process is already finished, we want to call the callback right away
    if (_running)
    {
        // process is running, store the callback
        _callback = callback;
    }
    else
    {
        // keep state
        int state = _state;
        
        // process is not running, we want to call the callback right away,
        // but use a small timeout to wait for the process to be ready
        _loop->onTimeout(0.0, [state, callback]() {
            
            // call the callback
            callback(state);
        });
    }
}

/**
 *  Get the file descriptor linked to the stdin of
 *  the running program.
 *
 *  This file descriptor can be used to write data
 *  to the running program.
 *
 *  @return the stdin file descriptor
 */
Fd &Process::stdin()
{
    // return the file descriptor to write to stdin
    return _stdin.write();
}

/**
 *  Register a handler for writability
 *
 *  Note that if you had already registered a handler before, then that one
 *  will be reset. Only your new handler will be called when the filedescriptor
 *  becomes writable
 *
 *  @param  callback    The callback to invoke when the process is ready to receive input
 *  @return WriteWatcher
 */
std::shared_ptr<WriteWatcher> Process::onWritable(const WriteCallback &callback)
{
    // pass it on to the wrapper
    return _stdin.onWritable(callback);
}

/**
 *  Get the file descriptor linked to the stdout
 *  of the running program.
 *
 *  This file descriptor can be used to read data
 *  that the running program outputs to the
 *  standard output.
 *
 *  @return the stdout file descriptor
 */
Fd &Process::stdout()
{
    // return the file descriptor to read from stdout
    return _stdout.read();
}

/**
 *  Register a handler for readability
 *
 *  Note that if you had already registered a handler before, then that one
 *  will be reset. Only your new handler will be called when the filedescriptor
 *  becomes readable
 *
 *  @param  callback    The callback to invoke when the process has generated output
 *  @return ReadWatcher
 */
std::shared_ptr<ReadWatcher> Process::onReadable(const ReadCallback &callback)
{
    // pass it on to the wrapper
    return _stdout.onReadable(callback);
}

/**
 *  Get the file descriptor linked to the stderr
 *  of the running program.
 *
 *  This file descriptor can be used to read data
 *  that the running program outputs to the
 *  standard error.
 *
 *  @return the stderr file descriptor
 */
Fd &Process::stderr()
{
    // return the file descriptor to read from stderr
    return _stderr.read();
}

/**
 *  Register a handler for errorability
 *
 *  Note that if you had already registered a handler before, then that one
 *  will be reset. Only your new handler will be called when the filedescriptor
 *  becomes readable
 *
 *  @param  callback    The callback to invoke when the process has generated errors
 *  @return ReadWatcher
 */
std::shared_ptr<ReadWatcher> Process::onError(const ReadCallback &callback)
{
    // pass it on to the wrapper
    return _stderr.onReadable(callback);
}

/**
 *  End namespace
 */
}
