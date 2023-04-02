//
//  VoodooPS2TrackpadCommon.h
//  VoodooPS2Trackpad
//
//  Created by Le Bao Hiep on 27/09/2020.
//  Copyright © 2020 Acidanthera. All rights reserved.
//

#ifndef VoodooPS2TrackpadCommon_h
#define VoodooPS2TrackpadCommon_h

#define TEST_BIT(x, y) ((x >> y) & 0x1)

void inline PS2DictSetNumber(OSDictionary *dict, const char *key, unsigned int num) {
    OSNumber *val = OSNumber::withNumber(num, 32);
    if (val != nullptr) {
        dict->setObject(key, val);
        val->release();
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// SimpleAverage Class Declaration
//

template <class T, int N>
class SimpleAverage
{
private:
    T m_buffer[N];
    int m_count;
    int m_sum;
    int m_index;
    
public:
    inline SimpleAverage() { reset(); }
    T filter(T data)
    {
        // add new entry to sum
        m_sum += data;
        // if full buffer, then we are overwriting, so subtract old from sum
        if (m_count == N)
            m_sum -= m_buffer[m_index];
        // new entry into buffer
        m_buffer[m_index] = data;
        // move index to next position with wrap around
        if (++m_index >= N)
            m_index = 0;
        // keep count moving until buffer is full
        if (m_count < N)
            ++m_count;
        // return average of current items
        return m_sum / m_count;
    }
    inline void reset()
    {
        m_count = 0;
        m_sum = 0;
        m_index = 0;
    }
    inline int count() const { return m_count; }
    inline int sum() const { return m_sum; }
    T oldest() const
    {
        // undefined if nothing in here, return zero
        if (m_count == 0)
            return 0;
        // if it is not full, oldest is at index 0
        // if full, it is right where the next one goes
        if (m_count < N)
            return m_buffer[0];
        else
            return m_buffer[m_index];
    }
    T newest() const
    {
        // undefined if nothing in here, return zero
        if (m_count == 0)
            return 0;
        // newest is index - 1, with wrap
        int index = m_index;
        if (--index < 0)
            index = m_count-1;
        return m_buffer[index];
    }
    T average() const
    {
        if (m_count == 0)
            return 0;
        return m_sum / m_count;
    }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// DecayingAverage Class Declaration
//

template <class T, class TT, int N1, int N2, int D>
class DecayingAverage
{
private:
    T m_last;
    bool m_lastvalid;
    
public:
    inline DecayingAverage() { reset(); }
    T filter(T data, int fingers)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * N1) / D + (last * N2) / D;
        m_lastvalid = true;
        m_last = (T)result;
        return m_last;
    }
    inline void reset()
    {
        m_lastvalid = false;
    }
};

template <class T, class TT, int N1, int N2, int D>
class UndecayAverage
{
private:
    T m_last;
    bool m_lastvalid;
    
public:
    inline UndecayAverage() { reset(); }
    T filter(T data)
    {
        TT result = data;
        TT last = m_last;
        if (m_lastvalid)
            result = (result * D) / N1 - (last * N2) / N1;
        m_lastvalid = true;
        m_last = (T)data;
        return (T)result;
    }
    inline void reset()
    {
        m_lastvalid = false;
    }
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Force Touch Modes
//

typedef enum {
    FORCE_TOUCH_DISABLED = 0,
    FORCE_TOUCH_BUTTON = 1,
    FORCE_TOUCH_THRESHOLD = 2,
    FORCE_TOUCH_VALUE = 3,
    FORCE_TOUCH_CUSTOM = 4
} ForceTouchMode;

#endif /* VoodooPS2TrackpadCommon_h */
