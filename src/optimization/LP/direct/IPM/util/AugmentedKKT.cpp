/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {
namespace lp {
namespace direct {

// Form 
//
//    | (x <> z)  A^T | | dx | = | -r_c - x <> r_mu |,
//    |    A       0  | | dy |   | -r_b             |
//
// where 
//
//    r_b  = A x - b,
//    r_c  = A^T y - z + c,
//    r_mu = x o z - tau e,
//
// and dz can be computed using
//
//   dz = - x <> (r_mu + z o dx)
//

template<typename Real>
void AugmentedKKT
( const Matrix<Real>& A, 
  const Matrix<Real>& x, const Matrix<Real>& z,
        Matrix<Real>& J, bool onlyLower )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKT"))
    const Int m = A.Height();
    const Int n = A.Width();

    Zeros( J, m+n, m+n );
    const IR xInd(0,n), yInd(n,n+m);
    auto Jxx = J(xInd,xInd); auto Jxy = J(xInd,yInd);
    auto Jyx = J(yInd,xInd); auto Jyy = J(yInd,yInd);
    Matrix<Real> d( z );
    DiagonalSolve( LEFT, NORMAL, x, d );
    Diagonal( Jxx, d );
    Jyx = A;
    if( !onlyLower )
        Transpose( A, Jxy );
}

template<typename Real>
void AugmentedKKT
( const AbstractDistMatrix<Real>& A, 
  const AbstractDistMatrix<Real>& x,    const AbstractDistMatrix<Real>& z,
        AbstractDistMatrix<Real>& JPre, bool onlyLower )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKT"))
    const Int m = A.Height();
    const Int n = A.Width();

    auto JPtr = WriteProxy<Real,MC,MR>(&JPre);
    auto& J = *JPtr;

    Zeros( J, m+n, m+n );
    const IR xInd(0,n), yInd(n,n+m);
    auto Jxx = J(xInd,xInd); auto Jxy = J(xInd,yInd);
    auto Jyx = J(yInd,xInd); auto Jyy = J(yInd,yInd);
    DistMatrix<Real,STAR,STAR> d( z );
    DiagonalSolve( LEFT, NORMAL, x, d );
    Diagonal( Jxx, d.Matrix() );
    Jyx = A;
    if( !onlyLower )
        Transpose( A, Jxy );
}

template<typename Real>
void AugmentedKKT
( const SparseMatrix<Real>& A, 
  const Matrix<Real>& x,       const Matrix<Real>& z,
        SparseMatrix<Real>& J, bool onlyLower )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKT"))
    const Int m = A.Height();
    const Int n = A.Width();
    const Int numEntries = A.NumEntries();

    Zeros( J, m+n, m+n );
    if( onlyLower )
        J.Reserve( numEntries + n );
    else
        J.Reserve( 2*numEntries + n ); 

    // x <> z updates
    for( Int j=0; j<n; ++j )
        J.Update( j, j, z.Get(j,0)/x.Get(j,0) );
    // A and A^T updates
    for( Int e=0; e<numEntries; ++e )
    {
        J.Update( A.Row(e)+n, A.Col(e), A.Value(e) );
        if( !onlyLower )
            J.Update( A.Col(e), A.Row(e)+n, A.Value(e) );
    }
    J.MakeConsistent();
}

template<typename Real>
void AugmentedKKT
( const DistSparseMatrix<Real>& A,
  const DistMultiVec<Real>& x,     const DistMultiVec<Real>& z,
        DistSparseMatrix<Real>& J, bool onlyLower )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKT"))
    const Int m = A.Height();
    const Int n = A.Width();

    mpi::Comm comm = A.Comm();
    const Int commSize = mpi::Size( comm );

    J.SetComm( comm );
    Zeros( J, m+n, m+n );

    // Compute the number of entries to send to each process
    // =====================================================
    std::vector<int> sendCounts(commSize,0);
    // For placing A into the bottom-left corner
    // -----------------------------------------
    for( Int e=0; e<A.NumLocalEntries(); ++e )
        ++sendCounts[ J.RowOwner(A.Row(e)+n) ];
    // For placing A^T into the top-right corner
    // -----------------------------------------
    DistSparseMatrix<Real> ATrans(comm);
    if( !onlyLower )
    {
        Transpose( A, ATrans );
        for( Int e=0; e<ATrans.NumLocalEntries(); ++e )
            ++sendCounts[ J.RowOwner(ATrans.Row(e)) ];
    }
    // For placing x <> z into the top-left corner
    // -------------------------------------------
    for( Int e=0; e<x.LocalHeight(); ++e )
        ++sendCounts[ J.RowOwner( e+x.FirstLocalRow() ) ];
    // Communicate to determine the number we receive from each process
    // ----------------------------------------------------------------
    std::vector<int> recvCounts(commSize);
    mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, comm );
    std::vector<int> sendOffsets, recvOffsets;
    int totalSend = Scan( sendCounts, sendOffsets );
    int totalRecv = Scan( recvCounts, recvOffsets );

    // Pack the triplets
    // =================
    std::vector<Int> sSendBuf(totalSend), tSendBuf(totalSend);
    std::vector<Real> vSendBuf(totalSend);
    auto offsets = sendOffsets;
    // Pack A
    // ------
    for( Int e=0; e<A.NumLocalEntries(); ++e )
    {
        const Int i = A.Row(e) + n;
        const Int j = A.Col(e);
        const Real value = A.Value(e);
        const Int owner = J.RowOwner(i);
        sSendBuf[offsets[owner]] = i;
        tSendBuf[offsets[owner]] = j;
        vSendBuf[offsets[owner]] = value;
        ++offsets[owner];
    }
    // Pack A^T
    // --------
    if( !onlyLower )
    {
        for( Int e=0; e<ATrans.NumLocalEntries(); ++e )
        {
            const Int i = ATrans.Row(e);
            const Int j = ATrans.Col(e) + n;
            const Real value = ATrans.Value(e);
            const Int owner = J.RowOwner(i);
            sSendBuf[offsets[owner]] = i;
            tSendBuf[offsets[owner]] = j;
            vSendBuf[offsets[owner]] = value;
            ++offsets[owner];
        }
    }
    // Pack x <> z
    // -----------
    for( Int e=0; e<x.LocalHeight(); ++e )
    {
        const Int i = e + x.FirstLocalRow();
        const Int j = i;
        const Real value = z.GetLocal(e,0)/x.GetLocal(e,0);
        const Int owner = J.RowOwner(i);
        sSendBuf[offsets[owner]] = i;
        tSendBuf[offsets[owner]] = j;
        vSendBuf[offsets[owner]] = value;
        ++offsets[owner];
    }

    // Exchange and unpack the triplets
    // ================================
    std::vector<Int> sRecvBuf(totalRecv), tRecvBuf(totalRecv);
    std::vector<Real> vRecvBuf(totalRecv);
    mpi::AllToAll
    ( sSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      sRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    mpi::AllToAll
    ( tSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      tRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    mpi::AllToAll
    ( vSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      vRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    J.Reserve( totalRecv );
    for( Int e=0; e<totalRecv; ++e )
        J.QueueLocalUpdate
        ( sRecvBuf[e]-J.FirstLocalRow(), tRecvBuf[e], vRecvBuf[e] );
    J.MakeConsistent();
}

template<typename Real>
void AugmentedKKTRHS
( const Matrix<Real>& x, 
  const Matrix<Real>& rc,  const Matrix<Real>& rb, 
  const Matrix<Real>& rmu,
        Matrix<Real>& d )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKTRHS"))
    const Int m = rb.Height();
    const Int n = rmu.Height();
    const IR xInd(0,n), yInd(n,n+m);
    Zeros( d, m+n, 1 );

    // dx := - (r_c + x <> r_mu)
    // =========================
    auto dx = d(xInd,IR(0,1));
    dx = rmu;
    DiagonalSolve( LEFT, NORMAL, x, dx );
    Axpy( Real(1), rc, dx );
    Scale( Real(-1), dx );

    // dy := -r_b
    // ==========
    auto dy = d(yInd,IR(0,1));
    dy = rb;
    Scale( Real(-1), dy );
}

template<typename Real>
void AugmentedKKTRHS
( const AbstractDistMatrix<Real>& xPre, 
  const AbstractDistMatrix<Real>& rc,   const AbstractDistMatrix<Real>& rb, 
  const AbstractDistMatrix<Real>& rmu,
        AbstractDistMatrix<Real>& dPre )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::AugmentedKKTRHS"))

    ProxyCtrl ctrl;
    ctrl.colConstrain = true;
    ctrl.rowConstrain = true;
    ctrl.colAlign = 0;
    ctrl.rowAlign = 0;
    
    auto xPtr = ReadProxy<Real,MC,MR>(&xPre,ctrl);  auto& x = *xPtr;
    auto dPtr = WriteProxy<Real,MC,MR>(&dPre,ctrl); auto& d = *dPtr;

    const Int m = rb.Height();
    const Int n = rmu.Height();
    const IR xInd(0,n), yInd(n,n+m);
    Zeros( d, m+n, 1 );

    // dx := - (r_c + x <> r_mu)
    // =========================
    auto dx = d(xInd,IR(0,1));
    dx = rmu;
    DiagonalSolve( LEFT, NORMAL, x, dx );
    Axpy( Real(1), rc, dx );
    Scale( Real(-1), dx );

    // dy := -r_b
    // ==========
    auto dy = d(yInd,IR(0,1));
    dy = rb;
    Scale( Real(-1), dy );
}

template<typename Real>
void AugmentedKKTRHS
( const DistMultiVec<Real>& x,
  const DistMultiVec<Real>& rc,  const DistMultiVec<Real>& rb, 
  const DistMultiVec<Real>& rmu, 
        DistMultiVec<Real>& d )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::FormAugmentedSystem"))
    const Int m = rb.Height();
    const Int n = x.Height();
    Zeros( d, m+n, 1 );
    mpi::Comm comm = x.Comm();
    const Int commSize = mpi::Size( comm );

    // Compute the number of entries to send/recv from each process
    // ============================================================
    std::vector<int> sendCounts(commSize,0);
    for( Int e=0; e<rc.LocalHeight(); ++e )
        ++sendCounts[ d.RowOwner( e+rc.FirstLocalRow() ) ];
    for( Int e=0; e<rb.LocalHeight(); ++e )
        ++sendCounts[ d.RowOwner( e+rb.FirstLocalRow()+n ) ];
    std::vector<int> recvCounts(commSize);
    mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, comm );
    std::vector<int> sendOffsets, recvOffsets;
    const int totalSend = Scan( sendCounts, sendOffsets );
    const int totalRecv = Scan( recvCounts, recvOffsets );

    // Pack the doublets
    // =================
    std::vector<int> sSendBuf(totalSend);
    std::vector<Real> vSendBuf(totalSend);
    auto offsets = sendOffsets;
    for( Int e=0; e<rc.LocalHeight(); ++e )
    {
        const Int i = e + rc.FirstLocalRow();
        const Real value = -rc.GetLocal(e,0)-rmu.GetLocal(e,0)/x.GetLocal(e,0);
        const Int owner = d.RowOwner(i);
        sSendBuf[offsets[owner]] = i;
        vSendBuf[offsets[owner]] = value;
        ++offsets[owner];
    }
    for( Int e=0; e<rb.LocalHeight(); ++e )
    {
        const Int i = e + rb.FirstLocalRow() + n;
        const Real value = -rb.GetLocal(e,0);
        const Int owner = d.RowOwner(i);
        sSendBuf[offsets[owner]] = i;
        vSendBuf[offsets[owner]] = value;
        ++offsets[owner];
    }

    // Exchange and unpack the doublets
    // ================================
    std::vector<int> sRecvBuf(totalRecv);
    std::vector<Real> vRecvBuf(totalRecv);
    mpi::AllToAll
    ( sSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      sRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    mpi::AllToAll
    ( vSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      vRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    for( Int e=0; e<totalRecv; ++e )
        d.UpdateLocal( sRecvBuf[e]-d.FirstLocalRow(), 0, vRecvBuf[e] );
}

template<typename Real>
void ExpandAugmentedSolution
( const Matrix<Real>& x,   const Matrix<Real>& z,
  const Matrix<Real>& rmu, const Matrix<Real>& d,
        Matrix<Real>& dx,        Matrix<Real>& dy, 
        Matrix<Real>& dz )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::ExpandAugmentedSolution"))
    const Int n = rmu.Height();
    const Int m = d.Height() - n;

    // Extract dx and dy from [dx; dy]
    // ===============================
    dx.Resize( n, 1 );
    dy.Resize( m, 1 );
    const IR xInd(0,n), yInd(n,n+m);
    auto d_x = d(xInd,IR(0,1));
    auto d_y = d(yInd,IR(0,1));
    dx = d_x;
    dy = d_y;

    // dz := - x <> (r_mu + z o dx)
    // ============================
    dz = dx;
    DiagonalScale( LEFT, NORMAL, z, dz );
    Axpy( Real(1), rmu, dz );
    DiagonalSolve( LEFT, NORMAL, x, dz );
    Scale( Real(-1), dz );
}

template<typename Real>
void ExpandAugmentedSolution
( const AbstractDistMatrix<Real>& xPre,   const AbstractDistMatrix<Real>& zPre,
  const AbstractDistMatrix<Real>& rmuPre, const AbstractDistMatrix<Real>& dPre,
        AbstractDistMatrix<Real>& dxPre,        AbstractDistMatrix<Real>& dy, 
        AbstractDistMatrix<Real>& dzPre )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::ExpandAugmentedSolution"))

    ProxyCtrl ctrl;
    ctrl.colConstrain = true;
    ctrl.rowConstrain = true;
    ctrl.colAlign = 0;
    ctrl.rowAlign = 0;

    auto xPtr = ReadProxy<Real,MC,MR>(&xPre,ctrl); auto& x = *xPtr;
    auto zPtr = ReadProxy<Real,MC,MR>(&zPre,ctrl); auto& z = *zPtr;

    auto rmuPtr = ReadProxy<Real,MC,MR>(&rmuPre); auto& rmu = *rmuPtr;
    auto dPtr   = ReadProxy<Real,MC,MR>(&dPre);   auto& d   = *dPtr;

    auto dxPtr = WriteProxy<Real,MC,MR>(&dxPre,ctrl); auto& dx = *dxPtr;
    auto dzPtr = WriteProxy<Real,MC,MR>(&dzPre,ctrl); auto& dz = *dzPtr;

    const Int n = rmu.Height();
    const Int m = d.Height() - n;

    // Extract dx and dy from [dx; dy]
    // ===============================
    dx.Resize( n, 1 );
    dy.Resize( m, 1 );
    const IR xInd(0,n), yInd(n,n+m);
    auto d_x = d(xInd,IR(0,1));
    auto d_y = d(yInd,IR(0,1));
    dx = d_x;
    Copy( d_y, dy );

    // dz := - x <> (r_mu + z o dx)
    // ============================
    dz = dx;
    DiagonalScale( LEFT, NORMAL, z, dz );
    Axpy( Real(1), rmu, dz );
    DiagonalSolve( LEFT, NORMAL, x, dz );
    Scale( Real(-1), dz );
}

template<typename Real>
void ExpandAugmentedSolution
( const DistMultiVec<Real>& x,   const DistMultiVec<Real>& z,
  const DistMultiVec<Real>& rmu, const DistMultiVec<Real>& d,
        DistMultiVec<Real>& dx,        DistMultiVec<Real>& dy, 
        DistMultiVec<Real>& dz )
{
    DEBUG_ONLY(CallStackEntry cse("lp::direct::ExpandAugmentedSolution"))
    const Int n = rmu.Height();
    const Int m = d.Height() - n;
    mpi::Comm comm = z.Comm();
    const Int commSize = mpi::Size(comm);

    // Extract dx and dy from [dx; dy]
    // ===============================
    dx.Resize( n, 1 );
    dy.Resize( m, 1 );
    // Compute the number of entries to send to each process
    // -----------------------------------------------------
    std::vector<int> sendCounts(commSize,0);
    for( Int iLoc=0; iLoc<d.LocalHeight(); ++iLoc )
    {
        const Int i = d.FirstLocalRow() + iLoc;
        if( i < n )
            ++sendCounts[ dx.RowOwner(i) ];
        else
            ++sendCounts[ dy.RowOwner(i-n) ];
    }
    // Communicate to determine the number we receive from each process
    // ----------------------------------------------------------------
    std::vector<int> recvCounts(commSize);
    mpi::AllToAll( sendCounts.data(), 1, recvCounts.data(), 1, comm );
    std::vector<int> sendOffsets, recvOffsets;
    int totalSend = Scan( sendCounts, sendOffsets );
    int totalRecv = Scan( recvCounts, recvOffsets );
    // Pack the entries and row indices of dx and dy
    // ---------------------------------------------
    std::vector<Int> sSendBuf(totalSend);
    std::vector<Real> vSendBuf(totalSend);
    auto offsets = sendOffsets;
    for( Int iLoc=0; iLoc<d.LocalHeight(); ++iLoc )
    {
        const Int i = d.FirstLocalRow() + iLoc;
        if( i < n )
        {
            const Int owner = dx.RowOwner(i); 
            sSendBuf[offsets[owner]] = i; 
            vSendBuf[offsets[owner]] = d.GetLocal(iLoc,0);
            ++offsets[owner]; 
        }
        else
        {
            const Int owner = dy.RowOwner(i-n);
            sSendBuf[offsets[owner]] = i;
            vSendBuf[offsets[owner]] = d.GetLocal(iLoc,0);
            ++offsets[owner];
        }
    }
    // Exchange and unpack the entries and indices
    // -------------------------------------------
    std::vector<Int> sRecvBuf(totalRecv);
    std::vector<Real> vRecvBuf(totalRecv);
    mpi::AllToAll
    ( sSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      sRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    mpi::AllToAll
    ( vSendBuf.data(), sendCounts.data(), sendOffsets.data(),
      vRecvBuf.data(), recvCounts.data(), recvOffsets.data(), comm );
    for( Int e=0; e<totalRecv; ++e )
    {
        const Int i = sRecvBuf[e];
        if( i < n )
            dx.SetLocal( i-dx.FirstLocalRow(), 0, vRecvBuf[e] );
        else
            dy.SetLocal( i-n-dy.FirstLocalRow(), 0, vRecvBuf[e] );
    }

    // dz := - x <> (r_mu + z o dx)
    // ============================
    dz = dx;
    DiagonalScale( NORMAL, z, dz );
    Axpy( Real(1), rmu, dz );
    DiagonalSolve( NORMAL, x, dz );
    Scale( Real(-1), dz );
}

#define PROTO(Real) \
  template void AugmentedKKT \
  ( const Matrix<Real>& A, \
    const Matrix<Real>& x, const Matrix<Real>& z, \
    Matrix<Real>& J, bool onlyLower ); \
  template void AugmentedKKT \
  ( const AbstractDistMatrix<Real>& A, \
    const AbstractDistMatrix<Real>& x, const AbstractDistMatrix<Real>& z, \
    AbstractDistMatrix<Real>& J, bool onlyLower ); \
  template void AugmentedKKT \
  ( const SparseMatrix<Real>& A, \
    const Matrix<Real>& x, const Matrix<Real>& z, \
    SparseMatrix<Real>& J, bool onlyLower ); \
  template void AugmentedKKT \
  ( const DistSparseMatrix<Real>& A, \
    const DistMultiVec<Real>& x, const DistMultiVec<Real>& z, \
    DistSparseMatrix<Real>& J, bool onlyLower ); \
  template void AugmentedKKTRHS \
  ( const Matrix<Real>& x, \
    const Matrix<Real>& rc, const Matrix<Real>& rb, \
    const Matrix<Real>& rmu, \
          Matrix<Real>& d ); \
  template void AugmentedKKTRHS \
  ( const AbstractDistMatrix<Real>& x, \
    const AbstractDistMatrix<Real>& rc, const AbstractDistMatrix<Real>& rb, \
    const AbstractDistMatrix<Real>& rmu, \
          AbstractDistMatrix<Real>& d ); \
  template void AugmentedKKTRHS \
  ( const DistMultiVec<Real>& x, \
    const DistMultiVec<Real>& rc, const DistMultiVec<Real>& rb, \
    const DistMultiVec<Real>& rmu, \
          DistMultiVec<Real>& d ); \
  template void ExpandAugmentedSolution \
  ( const Matrix<Real>& x, const Matrix<Real>& z, \
    const Matrix<Real>& rmu, const Matrix<Real>& d, \
    Matrix<Real>& dx, Matrix<Real>& dy, Matrix<Real>& dz ); \
  template void ExpandAugmentedSolution \
  ( const AbstractDistMatrix<Real>& x, const AbstractDistMatrix<Real>& z, \
    const AbstractDistMatrix<Real>& rmu, const AbstractDistMatrix<Real>& d, \
    AbstractDistMatrix<Real>& dx, AbstractDistMatrix<Real>& dy, \
    AbstractDistMatrix<Real>& dz ); \
  template void ExpandAugmentedSolution \
  ( const DistMultiVec<Real>& x, const DistMultiVec<Real>& z, \
    const DistMultiVec<Real>& rmu, const DistMultiVec<Real>& d, \
    DistMultiVec<Real>& dx, DistMultiVec<Real>& dy, DistMultiVec<Real>& dz );

#define EL_NO_INT_PROTO
#define EL_NO_COMPLEX_PROTO
#include "El/macros/Instantiate.h"

} // namespace direct
} // namespace lp
} // namespace El