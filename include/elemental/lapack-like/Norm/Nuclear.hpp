/*
   Copyright (c) 2009-2013, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#pragma once
#ifndef LAPACK_NORM_NUCLEAR_HPP
#define LAPACK_NORM_NUCLEAR_HPP

#include "elemental/lapack-like/Norm/Schatten.hpp"

namespace elem {

template<typename F> 
inline BASE(F)
NuclearNorm( const Matrix<F>& A )
{
#ifndef RELEASE
    PushCallStack("NuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = SchattenNorm( A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

template<typename F>
inline BASE(F)
HermitianNuclearNorm( UpperOrLower uplo, const Matrix<F>& A )
{
#ifndef RELEASE
    PushCallStack("HermitianNuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = HermitianSchattenNorm( uplo, A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

template<typename F>
inline BASE(F)
SymmetricNuclearNorm( UpperOrLower uplo, const Matrix<F>& A )
{
#ifndef RELEASE
    PushCallStack("SymmetricNuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = SymmetricSchattenNorm( uplo, A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

template<typename F,Distribution U,Distribution V> 
inline BASE(F)
NuclearNorm( const DistMatrix<F,U,V>& A )
{
#ifndef RELEASE
    PushCallStack("NuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = SchattenNorm( A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

template<typename F,Distribution U,Distribution V>
inline BASE(F)
HermitianNuclearNorm( UpperOrLower uplo, const DistMatrix<F,U,V>& A )
{
#ifndef RELEASE
    PushCallStack("HermitianNuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = HermitianSchattenNorm( uplo, A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

template<typename F,Distribution U,Distribution V>
inline BASE(F)
SymmetricNuclearNorm( UpperOrLower uplo, const DistMatrix<F,U,V>& A )
{
#ifndef RELEASE
    PushCallStack("SymmetricNuclearNorm");
#endif
    typedef BASE(F) R;
    const R norm = SymmetricSchattenNorm( uplo, A, R(1) );
#ifndef RELEASE
    PopCallStack();
#endif
    return norm;
}

} // namespace elem

#endif // ifndef LAPACK_NORM_NUCLEAR_HPP
