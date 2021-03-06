// AMD AMDUtils code
// 
// Copyright(c) 2017 Advanced Micro Devices, Inc.All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#pragma once

//
// This file holds all the structures/classes used to load a glTF model
//

#include "../json/json.h"
#include "GltfFeatures.h"
#include <DirectXMath.h>            // Find a replacemnt for this so that the GUI is not platform specific! look at using CMP_MATH

using namespace DirectX;

/* Notes
typedef struct _D3DMATRIX {
union {
         struct {
                float        _11, _12, _13, _14;
                float        _21, _22, _23, _24;
                float        _31, _32, _33, _34;
                float        _41, _42, _43, _44;
                };
         float m[4][4];
    };
}
*/

struct tfNode
{
    std::vector<tfNode *> m_children;

    int meshIndex = -1;

    XMMATRIX m_rotation;
    XMVECTOR m_translation;
    XMVECTOR m_scale;

    XMMATRIX GetWorldMat() { 
        return XMMatrixScalingFromVector(m_scale)  * m_rotation  * XMMatrixTranslationFromVector(m_translation); 
    }
};

struct tfScene
{
    std::vector<tfNode *> m_nodes;
};

class tfAccessor
{
public:
    void *m_data = NULL;
    int m_count = 0;
    int m_stride;
    int m_dimension;
    int m_type;

    XMVECTOR m_min;
    XMVECTOR m_max;

    void *Get(int i)
    {
        if (i >= m_count)
            i = m_count - 1;

        return (char*)m_data + m_stride*i;
    }

    int FindClosestFloatIndex(float val)
    {
        int ini = 0;
        int fin = m_count - 1;

        while (ini <= fin)
        {
            int mid = (ini + fin) / 2;
            float v = *(float*)Get(mid);

            if (v <= val)
                ini = mid + 1;
            else
                fin = mid - 1;
        }

        {
            if (*(float*)Get(fin) > val)
            {
                // Error !!
            }
            if (fin < m_count)
            {
                if (*(float*)Get(fin + 1) < val)
                {
                    // Error !!
                }
            }
        }

        return fin;
    }
};

class tfSampler
{
public:
    tfAccessor m_time;
    tfAccessor m_value;

    void SampleLinear(float time, float *frac, float **pCurr, float **pNext)
    {
        int curr_index = m_time.FindClosestFloatIndex(time);
        int next_index = min(curr_index + 1, m_time.m_count - 1);

        float curr_time = *(float*)m_time.Get(curr_index);
        float next_time = *(float*)m_time.Get(next_index);

        *frac = (time - curr_time) / (next_time - curr_time);
        *pCurr = (float*)m_value.Get(curr_index);
        *pNext = (float*)m_value.Get(next_index);
    }
};

class tfChannel
{
public:
    ~tfChannel()
    {
        delete m_pTranslation;
        delete m_pRotation;
        delete m_pScale;
    }

    tfSampler *m_pTranslation;
    tfSampler *m_pRotation;
    tfSampler *m_pScale;
};

struct tfAnimation
{
    float m_duration;
    std::map<int, tfChannel> m_channels;
};

