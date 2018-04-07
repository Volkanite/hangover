/*
 * Copyright 2017 André Hentschel
 * Copyright 2018 Stefan Dösinger for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/* NOTE: The guest side uses mingw's headers. The host side uses Wine's headers. */

#include <windows.h>
#include <stdio.h>
#include <commctrl.h>

#include "thunk/qemu_windows.h"
#include "thunk/qemu_commctrl.h"

#include "windows-user-services.h"
#include "dll_list.h"
#include "qemu_comctl32.h"
#include "istream_wrapper.h"

#ifndef QEMU_DLL_GUEST
#include <wine/debug.h>
WINE_DEFAULT_DEBUG_CHANNEL(qemu_comctl32);
#endif

struct qemu_DPA_LoadStream
{
    struct qemu_syscall super;
    uint64_t phDpa;
    uint64_t loadProc;
    uint64_t pStream;
    uint64_t pData;
    uint64_t wrapper;
};

struct qemu_DPA_LoadStream_cb
{
    uint64_t cb;
    uint64_t info;
    uint64_t stream;
    uint64_t data;
};

#ifdef QEMU_DLL_GUEST

static HRESULT __fastcall qemu_DPA_Stream_guest_cb(struct qemu_DPA_LoadStream_cb *call)
{
    PFNDPASTREAM cb = (PFNDPASTREAM)(ULONG_PTR)call->cb;
    return cb((DPASTREAMINFO *)(ULONG_PTR)call->info, (IStream *)(ULONG_PTR)call->stream, (void *)(ULONG_PTR)call->data);
}

WINBASEAPI HRESULT WINAPI DPA_LoadStream (HDPA *phDpa, PFNDPASTREAM loadProc, IStream *pStream, LPVOID pData)
{
    struct qemu_DPA_LoadStream call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_LOADSTREAM);
    call.phDpa = (ULONG_PTR)phDpa;
    call.loadProc = (ULONG_PTR)loadProc;
    call.pStream = (ULONG_PTR)pStream;
    call.pData = (ULONG_PTR)pData;
    call.wrapper = (ULONG_PTR)qemu_DPA_Stream_guest_cb;

    if (!phDpa || !loadProc || !pStream)
        return E_INVALIDARG;

    qemu_syscall(&call.super);
    *phDpa = (HDPA)(ULONG_PTR)call.phDpa;

    return call.super.iret;
}

#else

struct qemu_DPA_Stream_host_data
{
    uint64_t guest_cb, guest_data, wrapper;
};

static HRESULT CALLBACK qemu_DPA_Stream_host_cb(DPASTREAMINFO *info, IStream *stream, void *data)
{
    struct qemu_DPA_Stream_host_data *ctx = data;
    struct istream_wrapper *wrapper = istream_wrapper_from_IStream(stream);
    struct qemu_DPA_LoadStream_cb call;
    struct qemu_DPASTREAMINFO info32;
    HRESULT hr;

    call.cb = ctx->guest_cb;
#if GUEST_BIT == HOST_BIT
    call.info = QEMU_H2G(info);
#else
    DPASTREAMINFO_h2g(&info32, info);
    call.info = QEMU_H2G(&info32);
#endif
    call.stream = istream_wrapper_guest_iface(wrapper);
    call.data = ctx->guest_data;

    WINE_TRACE("Calling guest callback %p(%p, %p, %p).\n", (void *)call.cb, (void *)call.info,
            (void *)call.stream, (void *)call.data);
    hr = qemu_ops->qemu_execute(QEMU_G2H(ctx->wrapper), QEMU_H2G(&call));
    WINE_TRACE("Guest callback returned 0x%x.\n", hr);

#if GUEST_BIT != HOST_BIT
    DPASTREAMINFO_g2h(info, &info32);
#endif

    return hr;
}

void qemu_DPA_LoadStream(struct qemu_syscall *call)
{
    struct qemu_DPA_LoadStream *c = (struct qemu_DPA_LoadStream *)call;
    struct istream_wrapper *wrapper;
    struct qemu_DPA_Stream_host_data ctx;
    HDPA dpa;

    WINE_TRACE("\n");
    wrapper = istream_wrapper_create(c->pStream);
    if (!wrapper)
    {
        WINE_WARN("Out of memory\n");
        c->super.iret = E_OUTOFMEMORY;
        return;
    }
    ctx.guest_cb = c->loadProc;
    ctx.guest_data = c->pData;
    ctx.wrapper = c->wrapper;

    c->super.iret = p_DPA_LoadStream(&dpa, qemu_DPA_Stream_host_cb,
            istream_wrapper_host_iface(wrapper), &ctx);

    istream_wrapper_destroy(wrapper);

    c->phDpa = QEMU_H2G(dpa);
}

#endif

struct qemu_DPA_SaveStream
{
    struct qemu_syscall super;
    uint64_t hDpa;
    uint64_t saveProc;
    uint64_t pStream;
    uint64_t pData;
    uint64_t wrapper;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI HRESULT WINAPI DPA_SaveStream (HDPA hDpa, PFNDPASTREAM saveProc, IStream *pStream, LPVOID pData)
{
    struct qemu_DPA_SaveStream call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_SAVESTREAM);
    call.hDpa = (ULONG_PTR)hDpa;
    call.saveProc = (ULONG_PTR)saveProc;
    call.pStream = (ULONG_PTR)pStream;
    call.pData = (ULONG_PTR)pData;
    call.wrapper = (ULONG_PTR)qemu_DPA_Stream_guest_cb;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_SaveStream(struct qemu_syscall *call)
{
    struct qemu_DPA_SaveStream *c = (struct qemu_DPA_SaveStream *)call;
    struct istream_wrapper *wrapper;
    struct qemu_DPA_Stream_host_data ctx;

    WINE_TRACE("\n");
    wrapper = istream_wrapper_create(c->pStream);
    if (!wrapper && c->pStream)
    {
        WINE_WARN("Out of memory\n");
        c->super.iret = E_OUTOFMEMORY;
        return;
    }
    ctx.guest_cb = c->saveProc;
    ctx.guest_data = c->pData;
    ctx.wrapper = c->wrapper;

    c->super.iret = p_DPA_SaveStream(QEMU_G2H(c->hDpa), c->saveProc ? qemu_DPA_Stream_host_cb : NULL,
            istream_wrapper_host_iface(wrapper), &ctx);

    istream_wrapper_destroy(wrapper);
}

#endif

struct qemu_DPA_Merge
{
    struct qemu_syscall super;
    uint64_t hdpa1;
    uint64_t hdpa2;
    uint64_t dwFlags;
    uint64_t pfnCompare;
    uint64_t pfnMerge;
    uint64_t lParam;
    uint64_t cmp_wrapper;
    uint64_t merge_wrapper;
};

struct qemu_DPA_Merge_cb
{
    uint64_t cb, whut, p1, p2, ctx;
};

struct qemu_DPA_Search_cb
{
    uint64_t cb, p1, p2, ctx;
};

#ifdef QEMU_DLL_GUEST

static void * __fastcall DPA_Merge_guest_cb(struct qemu_DPA_Merge_cb *call)
{
    PFNDPAMERGE cb = (PFNDPAMERGE)(ULONG_PTR)call->cb;
    return cb(call->whut, (void *)(ULONG_PTR)call->p1, (void *)(ULONG_PTR)call->p2, call->ctx);
}

static INT __fastcall DPA_Search_guest_cb(struct qemu_DPA_Search_cb *call)
{
    PFNDPACOMPARE cb = (PFNDPACOMPARE)(ULONG_PTR)call->cb;
    return cb((void *)(ULONG_PTR)call->p1, (void *)(ULONG_PTR)call->p2, call->ctx);
}

WINBASEAPI BOOL WINAPI DPA_Merge (HDPA hdpa1, HDPA hdpa2, DWORD dwFlags, PFNDPACOMPARE pfnCompare, PFNDPAMERGE pfnMerge, LPARAM lParam)
{
    struct qemu_DPA_Merge call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_MERGE);
    call.hdpa1 = (ULONG_PTR)hdpa1;
    call.hdpa2 = (ULONG_PTR)hdpa2;
    call.dwFlags = dwFlags;
    call.pfnCompare = (ULONG_PTR)pfnCompare;
    call.pfnMerge = (ULONG_PTR)pfnMerge;
    call.lParam = lParam;
    call.cmp_wrapper = (ULONG_PTR)DPA_Search_guest_cb;
    call.merge_wrapper = (ULONG_PTR)DPA_Merge_guest_cb;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

struct qemu_DPA_Search_ctx
{
    uint64_t guest_search_cb, guest_merge_cb, guest_ctx, search_wrapper, merge_wrapper;
};

static INT CALLBACK DPA_Search_host_cb(void *p1, void *p2, LPARAM param)
{
    struct qemu_DPA_Search_ctx *ctx = (struct qemu_DPA_Search_ctx *)param;
    struct qemu_DPA_Search_cb call;
    INT ret;
    
    call.cb = ctx->guest_search_cb;
    call.p1 = QEMU_H2G(p1);
    call.p2 = QEMU_H2G(p2);
    call.ctx = ctx->guest_ctx;
    
    WINE_TRACE("Calling guest callback %p(%p, %p, %lx).\n", (void *)call.cb, p1, p2, param);
    ret = qemu_ops->qemu_execute(QEMU_G2H(ctx->search_wrapper), QEMU_H2G(&call));
    WINE_TRACE("Guest callback returned %d.\n", ret);
    
    return ret;
}

static void * CALLBACK DPA_Merge_host_cb(UINT whut, void *p1, void *p2, LPARAM param)
{
    struct qemu_DPA_Search_ctx *ctx = (struct qemu_DPA_Search_ctx *)param;
    struct qemu_DPA_Merge_cb call;
    void *ret;
    
    call.cb = ctx->guest_merge_cb;
    call.whut = whut;
    call.p1 = QEMU_H2G(p1);
    call.p2 = QEMU_H2G(p2);
    call.ctx = ctx->guest_ctx;
    
    WINE_TRACE("Calling guest callback %p(%u, %p, %p, %lx).\n", (void *)call.cb, whut, p1, p2, param);
    ret = QEMU_G2H(qemu_ops->qemu_execute(QEMU_G2H(ctx->merge_wrapper), QEMU_H2G(&call)));
    WINE_TRACE("Guest callback returned %p.\n", ret);
    
    return ret;
}

void qemu_DPA_Merge(struct qemu_syscall *call)
{
    struct qemu_DPA_Merge *c = (struct qemu_DPA_Merge *)call;
    struct qemu_DPA_Search_ctx ctx;

    WINE_TRACE("\n");
    ctx.guest_search_cb = c->pfnCompare;
    ctx.guest_merge_cb = c->pfnMerge;
    ctx.guest_ctx = c->lParam;
    ctx.search_wrapper = c->cmp_wrapper;
    ctx.merge_wrapper = c->merge_wrapper;

    c->super.iret = p_DPA_Merge(QEMU_G2H(c->hdpa1), QEMU_G2H(c->hdpa2), c->dwFlags,
            c->pfnCompare ? DPA_Search_host_cb : NULL, c->pfnMerge ? DPA_Merge_host_cb : NULL,
            (LPARAM)&ctx);
}

#endif

struct qemu_DPA_Destroy
{
    struct qemu_syscall super;
    uint64_t hdpa;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI BOOL WINAPI DPA_Destroy (HDPA hdpa)
{
    struct qemu_DPA_Destroy call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_DESTROY);
    call.hdpa = (ULONG_PTR)hdpa;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_Destroy(struct qemu_syscall *call)
{
    struct qemu_DPA_Destroy *c = (struct qemu_DPA_Destroy *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_Destroy(QEMU_G2H(c->hdpa));
}

#endif

struct qemu_DPA_Grow
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t nGrow;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI BOOL WINAPI DPA_Grow (HDPA hdpa, INT nGrow)
{
    struct qemu_DPA_Grow call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_GROW);
    call.hdpa = (ULONG_PTR)hdpa;
    call.nGrow = nGrow;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_Grow(struct qemu_syscall *call)
{
    struct qemu_DPA_Grow *c = (struct qemu_DPA_Grow *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_Grow(QEMU_G2H(c->hdpa), c->nGrow);
}

#endif

struct qemu_DPA_Clone
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t hdpaNew;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI HDPA WINAPI DPA_Clone (const HDPA hdpa, HDPA hdpaNew)
{
    struct qemu_DPA_Clone call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_CLONE);
    call.hdpa = (ULONG_PTR)hdpa;
    call.hdpaNew = (ULONG_PTR)hdpaNew;

    qemu_syscall(&call.super);

    return (HDPA)(ULONG_PTR)call.super.iret;
}

#else

void qemu_DPA_Clone(struct qemu_syscall *call)
{
    struct qemu_DPA_Clone *c = (struct qemu_DPA_Clone *)call;
    WINE_TRACE("\n");
    c->super.iret = QEMU_H2G(p_DPA_Clone(QEMU_G2H(c->hdpa), QEMU_G2H(c->hdpaNew)));
}

#endif

struct qemu_DPA_GetPtr
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t nIndex;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI LPVOID WINAPI DPA_GetPtr (HDPA hdpa, INT_PTR nIndex)
{
    struct qemu_DPA_GetPtr call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_GETPTR);
    call.hdpa = (ULONG_PTR)hdpa;
    call.nIndex = nIndex;

    qemu_syscall(&call.super);

    return (LPVOID)(ULONG_PTR)call.super.iret;
}

#else

void qemu_DPA_GetPtr(struct qemu_syscall *call)
{
    struct qemu_DPA_GetPtr *c = (struct qemu_DPA_GetPtr *)call;
    WINE_TRACE("\n");
    c->super.iret = (ULONG_PTR)p_DPA_GetPtr(QEMU_G2H(c->hdpa), c->nIndex);
}

#endif

struct qemu_DPA_GetPtrIndex
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t p;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI INT WINAPI DPA_GetPtrIndex (HDPA hdpa, LPCVOID p)
{
    struct qemu_DPA_GetPtrIndex call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_GETPTRINDEX);
    call.hdpa = (ULONG_PTR)hdpa;
    call.p = (ULONG_PTR)p;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_GetPtrIndex(struct qemu_syscall *call)
{
    struct qemu_DPA_GetPtrIndex *c = (struct qemu_DPA_GetPtrIndex *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_GetPtrIndex(QEMU_G2H(c->hdpa), QEMU_G2H(c->p));
}

#endif

struct qemu_DPA_InsertPtr
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t i;
    uint64_t p;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI INT WINAPI DPA_InsertPtr (HDPA hdpa, INT i, LPVOID p)
{
    struct qemu_DPA_InsertPtr call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_INSERTPTR);
    call.hdpa = (ULONG_PTR)hdpa;
    call.i = i;
    call.p = (ULONG_PTR)p;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_InsertPtr(struct qemu_syscall *call)
{
    struct qemu_DPA_InsertPtr *c = (struct qemu_DPA_InsertPtr *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_InsertPtr(QEMU_G2H(c->hdpa), c->i, QEMU_G2H(c->p));
}

#endif

struct qemu_DPA_SetPtr
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t i;
    uint64_t p;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI BOOL WINAPI DPA_SetPtr (HDPA hdpa, INT i, LPVOID p)
{
    struct qemu_DPA_SetPtr call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_SETPTR);
    call.hdpa = (ULONG_PTR)hdpa;
    call.i = i;
    call.p = (ULONG_PTR)p;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_SetPtr(struct qemu_syscall *call)
{
    struct qemu_DPA_SetPtr *c = (struct qemu_DPA_SetPtr *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_SetPtr(QEMU_G2H(c->hdpa), c->i, QEMU_G2H(c->p));
}

#endif

struct qemu_DPA_DeletePtr
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t i;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI LPVOID WINAPI DPA_DeletePtr (HDPA hdpa, INT i)
{
    struct qemu_DPA_DeletePtr call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_DELETEPTR);
    call.hdpa = (ULONG_PTR)hdpa;
    call.i = i;

    qemu_syscall(&call.super);

    return (LPVOID)(ULONG_PTR)call.super.iret;
}

#else

void qemu_DPA_DeletePtr(struct qemu_syscall *call)
{
    struct qemu_DPA_DeletePtr *c = (struct qemu_DPA_DeletePtr *)call;
    WINE_TRACE("\n");
    c->super.iret = (ULONG_PTR)p_DPA_DeletePtr(QEMU_G2H(c->hdpa), c->i);
}

#endif

struct qemu_DPA_DeleteAllPtrs
{
    struct qemu_syscall super;
    uint64_t hdpa;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI BOOL WINAPI DPA_DeleteAllPtrs (HDPA hdpa)
{
    struct qemu_DPA_DeleteAllPtrs call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_DELETEALLPTRS);
    call.hdpa = (ULONG_PTR)hdpa;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_DeleteAllPtrs(struct qemu_syscall *call)
{
    struct qemu_DPA_DeleteAllPtrs *c = (struct qemu_DPA_DeleteAllPtrs *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_DeleteAllPtrs(QEMU_G2H(c->hdpa));
}

#endif

struct qemu_DPA_Sort
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t pfnCompare;
    uint64_t lParam;
    uint64_t wrapper;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI BOOL WINAPI DPA_Sort (HDPA hdpa, PFNDPACOMPARE pfnCompare, LPARAM lParam)
{
    struct qemu_DPA_Sort call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_SORT);
    call.hdpa = (ULONG_PTR)hdpa;
    call.pfnCompare = (ULONG_PTR)pfnCompare;
    call.lParam = lParam;
    call.wrapper = (ULONG_PTR)DPA_Search_guest_cb;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_Sort(struct qemu_syscall *call)
{
    struct qemu_DPA_Sort *c = (struct qemu_DPA_Sort *)call;
    struct qemu_DPA_Search_ctx ctx;
    WINE_TRACE("\n");

    ctx.guest_search_cb = c->pfnCompare;
    ctx.guest_ctx = c->lParam;
    ctx.search_wrapper = c->wrapper;

    c->super.iret = p_DPA_Sort(QEMU_G2H(c->hdpa), c->pfnCompare ? DPA_Search_host_cb : NULL, (LPARAM)&ctx);
}

#endif

struct qemu_DPA_Search
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t pFind;
    uint64_t nStart;
    uint64_t pfnCompare;
    uint64_t lParam;
    uint64_t uOptions;
    uint64_t wrapper;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI INT WINAPI DPA_Search (HDPA hdpa, LPVOID pFind, INT nStart, PFNDPACOMPARE pfnCompare, LPARAM lParam, UINT uOptions)
{
    struct qemu_DPA_Search call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_SEARCH);
    call.hdpa = (ULONG_PTR)hdpa;
    call.pFind = (ULONG_PTR)pFind;
    call.nStart = nStart;
    call.pfnCompare = (ULONG_PTR)pfnCompare;
    call.lParam = lParam;
    call.uOptions = uOptions;
    call.wrapper = (ULONG_PTR)DPA_Search_guest_cb;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_Search(struct qemu_syscall *call)
{
    struct qemu_DPA_Search *c = (struct qemu_DPA_Search *)call;
    struct qemu_DPA_Search_ctx ctx;
    WINE_TRACE("\n");

    ctx.guest_search_cb = c->pfnCompare;
    ctx.guest_ctx = c->lParam;
    ctx.search_wrapper = c->wrapper;

    c->super.iret = p_DPA_Search(QEMU_G2H(c->hdpa), QEMU_G2H(c->pFind), c->nStart,
            c->pfnCompare ? DPA_Search_host_cb : NULL, (LPARAM)&ctx, c->uOptions);
}

#endif

struct qemu_DPA_CreateEx
{
    struct qemu_syscall super;
    uint64_t nGrow;
    uint64_t hHeap;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI HDPA WINAPI DPA_CreateEx (INT nGrow, HANDLE hHeap)
{
    struct qemu_DPA_CreateEx call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_CREATEEX);
    call.nGrow = nGrow;
    call.hHeap = (ULONG_PTR)hHeap;

    qemu_syscall(&call.super);

    return (HDPA)(ULONG_PTR)call.super.iret;
}

#else

void qemu_DPA_CreateEx(struct qemu_syscall *call)
{
    struct qemu_DPA_CreateEx *c = (struct qemu_DPA_CreateEx *)call;
    WINE_TRACE("\n");
    c->super.iret = QEMU_H2G(p_DPA_CreateEx(c->nGrow, QEMU_G2H(c->hHeap)));
}

#endif

struct qemu_DPA_Create
{
    struct qemu_syscall super;
    uint64_t nGrow;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI HDPA WINAPI DPA_Create (INT nGrow)
{
    struct qemu_DPA_Create call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_CREATE);
    call.nGrow = nGrow;

    qemu_syscall(&call.super);

    return (HDPA)(ULONG_PTR)call.super.iret;
}

#else

void qemu_DPA_Create(struct qemu_syscall *call)
{
    struct qemu_DPA_Create *c = (struct qemu_DPA_Create *)call;
    WINE_TRACE("\n");
    c->super.iret = QEMU_H2G(p_DPA_Create(c->nGrow));
}

#endif

struct qemu_DPA_EnumCallback
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t enumProc;
    uint64_t lParam;
    uint64_t wrapper;
};

struct qemu_DPA_EnumCallback_cb
{
    uint64_t cb, item, ctx;
};

#ifdef QEMU_DLL_GUEST

static INT __fastcall DPA_EnumCallback_guest_cb(struct qemu_DPA_EnumCallback_cb *call)
{
    PFNDPAENUMCALLBACK cb = (PFNDPAENUMCALLBACK)(ULONG_PTR)call->cb;
    return cb((void *)(ULONG_PTR)call->item, (void *)(ULONG_PTR)call->ctx);
}

WINBASEAPI VOID WINAPI DPA_EnumCallback (HDPA hdpa, PFNDPAENUMCALLBACK enumProc, LPVOID lParam)
{
    struct qemu_DPA_EnumCallback call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_ENUMCALLBACK);
    call.hdpa = (ULONG_PTR)hdpa;
    call.enumProc = (ULONG_PTR)enumProc;
    call.lParam = (ULONG_PTR)lParam;
    call.wrapper = (ULONG_PTR)DPA_EnumCallback_guest_cb;

    qemu_syscall(&call.super);
}

#else

struct qemu_DPA_EnumCallback_host_ctx
{
    uint64_t guest_cb, guest_ctx, wrapper;
};

static INT CALLBACK qemu_DPA_EnumCallback_host_cb(void *item, void *param)
{
    struct qemu_DPA_EnumCallback_host_ctx *ctx = param;
    struct qemu_DPA_EnumCallback_cb call;
    INT ret;

    call.cb = ctx->guest_cb;
    call.item = QEMU_H2G(item);
    call.ctx = ctx->guest_ctx;

    WINE_TRACE("Calling guest callback %p(%p, %p).\n", (void *)call.cb, item, (void *)call.ctx);
    ret = qemu_ops->qemu_execute(QEMU_G2H(ctx->wrapper), QEMU_H2G(&call));
    WINE_TRACE("Guest callback returned %d.\n", ret);

    return ret;
}

void qemu_DPA_EnumCallback(struct qemu_syscall *call)
{
    struct qemu_DPA_EnumCallback *c = (struct qemu_DPA_EnumCallback *)call;
    struct qemu_DPA_EnumCallback_host_ctx ctx;

    WINE_TRACE("\n");
    ctx.guest_cb = c->enumProc;
    ctx.guest_ctx = c->lParam;
    ctx.wrapper = c->wrapper;

    p_DPA_EnumCallback(QEMU_G2H(c->hdpa), c->enumProc ? qemu_DPA_EnumCallback_host_cb : NULL, &ctx);
}

#endif

struct qemu_DPA_DestroyCallback
{
    struct qemu_syscall super;
    uint64_t hdpa;
    uint64_t enumProc;
    uint64_t lParam;
    uint64_t wrapper;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI void WINAPI DPA_DestroyCallback (HDPA hdpa, PFNDPAENUMCALLBACK enumProc, LPVOID lParam)
{
    struct qemu_DPA_DestroyCallback call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_DESTROYCALLBACK);
    call.hdpa = (ULONG_PTR)hdpa;
    call.enumProc = (ULONG_PTR)enumProc;
    call.lParam = (ULONG_PTR)lParam;
    call.wrapper = (ULONG_PTR)DPA_EnumCallback_guest_cb;

    qemu_syscall(&call.super);
}

#else

void qemu_DPA_DestroyCallback(struct qemu_syscall *call)
{
    struct qemu_DPA_DestroyCallback *c = (struct qemu_DPA_DestroyCallback *)call;
    struct qemu_DPA_EnumCallback_host_ctx ctx;

    WINE_TRACE("\n");
    ctx.guest_cb = c->enumProc;
    ctx.guest_ctx = c->lParam;
    ctx.wrapper = c->wrapper;

    p_DPA_DestroyCallback(QEMU_G2H(c->hdpa), c->enumProc ? qemu_DPA_EnumCallback_host_cb : NULL, &ctx);
}

#endif

struct qemu_DPA_GetSize
{
    struct qemu_syscall super;
    uint64_t hdpa;
};

#ifdef QEMU_DLL_GUEST

WINBASEAPI ULONGLONG WINAPI DPA_GetSize(HDPA hdpa)
{
    struct qemu_DPA_GetSize call;
    call.super.id = QEMU_SYSCALL_ID(CALL_DPA_GETSIZE);
    call.hdpa = (ULONG_PTR)hdpa;

    qemu_syscall(&call.super);

    return call.super.iret;
}

#else

void qemu_DPA_GetSize(struct qemu_syscall *call)
{
    struct qemu_DPA_GetSize *c = (struct qemu_DPA_GetSize *)call;
    WINE_TRACE("\n");
    c->super.iret = p_DPA_GetSize(QEMU_G2H(c->hdpa));
}

#endif

