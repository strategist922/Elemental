/*
   Copyright (c) 2009-2014, Jack Poulson, Lexing Ying,
   The University of Texas at Austin, Stanford University, and the
   Georgia Insitute of Technology.
   All rights reserved.
 
   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {

// Constructors and destructors
// ============================

template<typename T>
DistSparseMatrix<T>::DistSparseMatrix()
{ }

template<typename T>
DistSparseMatrix<T>::DistSparseMatrix( mpi::Comm comm )
: distGraph_(comm)
{ }

template<typename T>
DistSparseMatrix<T>::DistSparseMatrix( Int height, mpi::Comm comm )
: distGraph_(height,comm)
{ }

template<typename T>
DistSparseMatrix<T>::DistSparseMatrix( Int height, Int width, mpi::Comm comm )
: distGraph_(height,width,comm)
{ }

template<typename T>
DistSparseMatrix<T>::~DistSparseMatrix()
{ }

// Assignment and reconfiguration
// ==============================

// Make a copy
// -----------
// TODO

// Change the matrix size
// ----------------------
template<typename T>
void DistSparseMatrix<T>::Empty()
{
    distGraph_.Empty();
    SwapClear( vals_ );
    multMeta.Clear();
}

template<typename T>
void DistSparseMatrix<T>::Resize( int height, int width )
{
    distGraph_.Resize( height, width );
    SwapClear( vals_ );
}

// Change the distribution
// -----------------------
template<typename T>
void DistSparseMatrix<T>::SetComm( mpi::Comm comm )
{ 
    distGraph_.SetComm( comm ); 
    SwapClear( vals_ );
}

// Assembly
// --------
template<typename T>
void DistSparseMatrix<T>::Reserve( Int numLocalEntries )
{ 
    distGraph_.Reserve( numLocalEntries );
    vals_.reserve( numLocalEntries );
}

template<typename T>
void DistSparseMatrix<T>::Update( Int row, Int col, T value )
{
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::Update"))
    QueueUpdate( row, col, value );
    MakeConsistent();
}

template<typename T>
void DistSparseMatrix<T>::QueueUpdate( Int row, Int col, T value )
{
    DEBUG_ONLY(
        CallStackEntry cse("DistSparseMatrix::QueueUpdate");
        AssertConsistentSizes();
    )
    distGraph_.QueueConnection( row, col );
    vals_.push_back( value );
    multMeta.ready = false;
}

template<typename T>
void DistSparseMatrix<T>::MakeConsistent()
{
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::MakeConsistent"))

    if( !distGraph_.consistent_ )
    {
        const Int numLocalEntries = vals_.size();
        std::vector<Entry<T>> entries( numLocalEntries );
        for( Int s=0; s<numLocalEntries; ++s )
        {
            entries[s].indices[0] = distGraph_.sources_[s];
            entries[s].indices[1] = distGraph_.targets_[s];
            entries[s].value = vals_[s];
        }
        std::sort( entries.begin(), entries.end(), CompareEntries );

        // Compress out duplicates
        Int lastUnique=0;
        for( Int s=1; s<numLocalEntries; ++s )
        {
            if( entries[s].indices[0] != entries[lastUnique].indices[0] ||
                entries[s].indices[1] != entries[lastUnique].indices[1] )
            {
                ++lastUnique;
                entries[lastUnique].indices[0] = entries[s].indices[0];
                entries[lastUnique].indices[1] = entries[s].indices[1];
                entries[lastUnique].value = entries[s].value;
            }
            else
                entries[lastUnique].value += entries[s].value;
        }
        const Int numUnique = lastUnique+1;

        distGraph_.sources_.resize( numUnique );
        distGraph_.targets_.resize( numUnique );
        vals_.resize( numUnique );
        for( int s=0; s<numUnique; ++s )
        {
            distGraph_.sources_[s] = entries[s].indices[0];
            distGraph_.targets_[s] = entries[s].indices[1];
            vals_[s] = entries[s].value;
        }

        distGraph_.ComputeLocalEdgeOffsets();

        distGraph_.consistent_ = true;
    }
}

// Queries
// =======

// High-level information
// ----------------------
template<typename T>
Int DistSparseMatrix<T>::Height() const { return distGraph_.NumSources(); }
template<typename T>
Int DistSparseMatrix<T>::Width() const { return distGraph_.NumTargets(); }

template<typename T>
El::DistGraph& DistSparseMatrix<T>::DistGraph() { return distGraph_; }
template<typename T>
const El::DistGraph& DistSparseMatrix<T>::LockedDistGraph() const
{ return distGraph_; }

template<typename T>
Int DistSparseMatrix<T>::FirstLocalRow() const
{ return distGraph_.FirstLocalSource(); }

template<typename T>
Int DistSparseMatrix<T>::LocalHeight() const
{ return distGraph_.NumLocalSources(); }

template<typename T>
Int DistSparseMatrix<T>::NumLocalEntries() const
{
    DEBUG_ONLY(
        CallStackEntry cse("DistSparseMatrix::NumLocalEntries");
        AssertConsistentSizes();
    )
    return distGraph_.NumLocalEdges();
}

template<typename T>
Int DistSparseMatrix<T>::Capacity() const
{
    DEBUG_ONLY(
        CallStackEntry cse("DistSparseMatrix::Capacity");
        AssertConsistentSizes();
        AssertConsistentCapacities();
    )
    return distGraph_.Capacity();
}

template<typename T>
bool DistSparseMatrix<T>::Consistent() const
{ return distGraph_.Consistent(); }

// Distribution information
// ------------------------
template<typename T>
mpi::Comm DistSparseMatrix<T>::Comm() const { return distGraph_.Comm(); }
template<typename T>
Int DistSparseMatrix<T>::Blocksize() const { return distGraph_.Blocksize(); }

// Detailed local information
// --------------------------
template<typename T>
Int DistSparseMatrix<T>::Row( Int localInd ) const
{ 
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::Row"))
    return distGraph_.Source( localInd );
}

template<typename T>
Int DistSparseMatrix<T>::Col( Int localInd ) const
{ 
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::Col"))
    return distGraph_.Target( localInd );
}

template<typename T>
Int DistSparseMatrix<T>::LocalEntryOffset( Int localRow ) const
{
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::LocalEntryOffset"))
    return distGraph_.LocalEdgeOffset( localRow );
}

template<typename T>
Int DistSparseMatrix<T>::NumConnections( Int localRow ) const
{
    DEBUG_ONLY(CallStackEntry cse("DistSparseMatrix::NumConnections"))
    return distGraph_.NumConnections( localRow );
}

template<typename T>
T DistSparseMatrix<T>::Value( Int localInd ) const
{ 
    DEBUG_ONLY(
        CallStackEntry cse("DistSparseMatrix::Value");
        if( localInd < 0 || localInd >= (Int)vals_.size() )
            LogicError("Entry number out of bounds");
        AssertConsistent();
    )
    return vals_[localInd];
}

template<typename T>
Int* DistSparseMatrix<T>::SourceBuffer() { return distGraph_.SourceBuffer(); }
template<typename T>
Int* DistSparseMatrix<T>::TargetBuffer() { return distGraph_.TargetBuffer(); }
template<typename T>
T* DistSparseMatrix<T>::ValueBuffer() { return &vals_[0]; }

template<typename T>
const Int* DistSparseMatrix<T>::LockedSourceBuffer() const
{ return distGraph_.LockedSourceBuffer(); }

template<typename T>
const Int* DistSparseMatrix<T>::LockedTargetBuffer() const
{ return distGraph_.LockedTargetBuffer(); }

template<typename T>
const T* DistSparseMatrix<T>::LockedValueBuffer() const
{ return &vals_[0]; }

// Auxiliary routines
// ==================

template<typename T>
bool DistSparseMatrix<T>::CompareEntries( const Entry<T>& a, const Entry<T>& b )
{ return a.indices[0] < b.indices[0] || 
         (a.indices[0] == b.indices[0] && a.indices[1] < b.indices[1]); }

template<typename T>
void DistSparseMatrix<T>::AssertConsistent() const
{ 
    if( !Consistent() )
        LogicError("Distributed sparse matrix must be consistent");
}

template<typename T>
void DistSparseMatrix<T>::AssertConsistentSizes() const
{ 
    distGraph_.AssertConsistentSizes();
    if( distGraph_.NumLocalEdges() != (int)vals_.size() )
        LogicError("Inconsistent sparsity sizes");
}

template<typename T>
void DistSparseMatrix<T>::AssertConsistentCapacities() const
{ 
    distGraph_.AssertConsistentCapacities();
    if( distGraph_.Capacity() != vals_.capacity() )
        LogicError("Inconsistent sparsity capacities");
}

#define PROTO(T) template class DistSparseMatrix<T>;
#include "El/macros/Instantiate.h"

} // namespace El
