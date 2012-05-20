// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <boost/filesystem.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>

#include "ui_interface.h"
#include "qtipcserver.h"

using namespace boost;
using namespace std;

/** global state definition */
ipcState globalIpcState = IPC_NOT_INITIALIZED;

bool ipcRemove(const char* pszFilename)
{
    string logMessage = ("ipcRemove - possible stale message queue detected, trying to remove");

    /** try to remove the possible stale message queue */
    if (interprocess::message_queue::remove(IPC_MQ_NAME))
    {
        printf("%s %s ...success\n", logMessage.c_str(), pszFilename);
        return true;
    }
    else
    {
        printf("%s %s ...failed\n", logMessage.c_str(), pszFilename);
        return false;
    }
}

void ipcThread(void* pArg)
{
    IMPLEMENT_RANDOMIZE_STACK(ipcThread(pArg));
    try
    {
        ipcThread2(pArg);
    }
    catch (std::exception& e) {
        PrintExceptionContinue(&e, "ipcThread()");
    } catch (...) {
        PrintExceptionContinue(NULL, "ipcThread()");
    }
    printf("ipcThread exiting\n");
}

void ipcThread2(void* pArg)
{
    printf("ipcThread started\n");

    interprocess::message_queue* mq = (interprocess::message_queue*)pArg;
    char buffer[IPC_MQ_MAX_MESSAGE_SIZE + 1] = "";
    size_t nSize = 0;
    unsigned int nPriority = 0;

    loop
    {
        if (mq->try_receive(&buffer, sizeof(buffer), nSize, nPriority))
        {
            ThreadSafeHandleURI(std::string(buffer, nSize));
            Sleep(1000);
        }
        else
            /** needs to be here to stop this thread from utilizing 100% of a CPU core */
            Sleep(100);

        if (fShutdown)
            break;
    }

    /** cleanup allocated memory and set global IPC state to not initialized */
    delete mq;
    globalIpcState = IPC_NOT_INITIALIZED;
}

void ipcInit(bool fUseMQModeOpenOnly, bool fInitCalledAfterRecovery)
{
    /** set global IPC state variable to not initialized */
    globalIpcState = IPC_NOT_INITIALIZED;

    interprocess::message_queue* mq = NULL;

    try {
        if (fUseMQModeOpenOnly)
            mq = new interprocess::message_queue(interprocess::open_only, IPC_MQ_NAME);
        else
            mq = new interprocess::message_queue(interprocess::create_only, IPC_MQ_NAME, IPC_MQ_MAX_MESSAGES, IPC_MQ_MAX_MESSAGE_SIZE);
    }
    catch (interprocess::interprocess_exception &ex) {
#ifdef WIN32
        /** check if the exception is a "file not found" error */
        if(ex.get_error_code() == interprocess::not_found_error)
        {
            if (!fInitCalledAfterRecovery)
            {
                printf("ipcInit - trying to create new message queue...\n");

                /** try init once more (false - create_only mode / true - avoid an infinite recursion)
                 * create_only: create new message queue
                 */
                ipcInit(false, true);
            }
        }
        /** check if the exception is a "file already exists" error */
        else if (ex.get_error_code() == interprocess::already_exists_error)
        {
            if (!fInitCalledAfterRecovery)
            {
                printf("ipcInit - trying to open current message queue...\n");

                /** try init once more (true - open_only mode / true - avoid an infinite recursion)
                 * open_only: try to open the existing queue
                 */
                ipcInit(true, true);
            }
        }
        else
            printf("ipcInit - boost interprocess exception #%d: %s\n", ex.get_error_code(), ex.what());
#endif
        return;
    }

    if (!CreateThread(ipcThread, mq))
    {
        delete mq;
        return;
    }

    /** if we reach this, set global IPC state to initialized */
    globalIpcState = IPC_INITIALIZED;
}
