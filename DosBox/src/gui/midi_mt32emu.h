#ifndef _MIDI_MT32EMU_H
#define _MIDI_MT32EMU_H

#include <mt32emu/mt32emu.h>
#include <pthread.h>


template<typename T>
class MT32QNode
{
    public:
        T               value;
        MT32QNode<T> *  pNext;

        MT32QNode<T>(const T& val)
        {
            pNext = NULL;
            value = val;
        }

        MT32QNode<T>()
        {
            pNext = NULL;
        }

        ~MT32QNode()
        {
            if (pNext)
            {
                delete pNext;
            }
        }

};

template<typename T>
class MT32Q
{
        pthread_mutexattr_t     m_mtxAttr;
        pthread_mutex_t         m_mtx;
        MT32QNode<T> *          m_pHead;
        MT32QNode<T> *          m_pTail;

    public:

        MT32Q()
        {
            m_pHead = NULL;
            m_pTail = NULL;
            pthread_mutexattr_init(&m_mtxAttr);
            pthread_mutex_init(&m_mtx, &m_mtxAttr);
        }

        ~MT32Q()
        {
            pthread_mutex_destroy(&m_mtx);
            pthread_mutexattr_destroy(&m_mtxAttr);
            if (m_pHead)
            {
                delete m_pHead;
            }
        }

        void Pop(T *pOutVal)
        {
            if (m_pHead)
            {
                *pOutVal = m_pHead->value;
                // If the head is the tail, and we are popping the head,
                // then there is no tail left.
                //
                if (m_pTail == m_pHead)
                {
                    m_pTail = NULL;
                }
                MT32Q<T> *pOldHead = m_pHead;
                m_pHead = m_pHead->pNext;
                delete pOldHead;
            }
            else
            {
                assert(!"Tried to pop emtpy queue!");
            }
        }

        void Push(const T& inVal)
        {
            if (m_pTail)
            {
                m_pTail->pNext = new MT32QNode<T>(inVal);
                m_pTail = m_pTail->pNext;
            }
            else
            {
                assert(m_pHead == NULL);
                m_pTail = new MT32QNode<T>(inVal);
                m_pHead = m_pTail;
            }
        }

        void ThreadSafePush(const T& inVal)
        {
            pthread_mutex_lock(&m_mtx);
            Push(inVal);
            pthread_mutex_unlock(&m_mtx);
        }

        void ThreadSafePop(T *pOutVal)
        {
            pthread_mutex_lock(&m_mtx);
            Pop(pOutVal);
            pthread_mutex_unlock(&m_mtx);
        }
};



class MuntReportHandler : public MT32Emu::ReportHandler
{
    public:
        virtual void printDebug(const char *fmt, va_list list)
        {
            LOG_MSG(fmt, list);
        }
};

void *MuntThreadEntry(void *param)
{
    MT32Q<Bit32u> *pMsgQ = (MT32Q<Bit32u>*)param;
    MuntReportHandler report;

    MT32Emu::Synth *pSynth = new MT32Emu::Synth(&report);


    return NULL;
}





class MidiHandler_MT32Emu : public MidiHandler
{

        MT32Q<Bit32u>   m_msgQ;
        pthread_t       m_threadHandle;
        int             m_threadID;
        pthread_attr_t  m_threadAttr;

    public:

        MidiHandler_MT32Emu();
        virtual ~MidiHandler_MT32Emu()
        {
            void *retVal = NULL;

            m_msgQ.ThreadSafePush(-1);

            if (m_threadHandle)
            {
                pthread_join(m_threadHandle, &retVal);
            }

            pthread_attr_destroy(&m_threadAttr);
        }

        // Inherited from MidiHandler
        //
        virtual bool Open(const char * /*conf*/);
        virtual void Close(void);
        virtual void PlayMsg(Bit8u * /*msg*/);
        virtual void PlaySysex(Bit8u * /*sysex*/,Bitu /*len*/);
        virtual const char * GetName(void);


        // Inherited from ReportHandler
        //
        virtual void printDebug(const char *fmt, va_list list)
        {
            fprintf(stderr, fmt, list);
        }
};



bool MidiHandler_MT32Emu::Open(const char *conf)
{
    pthread_attr_init(&m_threadAttr);
//    pthread_attr_setdetachstate(&m_threadAttr, PTHREAD_CREATE_DETACHED);

    pthread_create(&m_threadHandle, &m_threadAttr, MuntThreadEntry, &m_msgQ);

}

void MidiHandler_MT32Emu::Close(void)
{
}

void MidiHandler_MT32Emu::PlayMsg(Bit8u *msg)
{
    m_msgQ.ThreadSafePush(*((Bit32u*)msg));
}

void MidiHandler_MT32Emu::PlaySysex(Bit8u *sysex,Bitu len)
{
}

const char * MidiHandler_MT32Emu::GetName(void)
{
}



#endif  // _MIDI_MT32EMU_H
