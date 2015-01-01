#
#  Copyright (c) 2009-2014, Jack Poulson
#  All rights reserved.
#
#  This file is part of Elemental and is under the BSD 2-Clause License, 
#  which can be found in the LICENSE file in the root directory, or at 
#  http://opensource.org/licenses/BSD-2-Clause
#
import El
import time

m = 2000
n = 4000
testIPF = True
testMehrotra = True
display = False
worldRank = El.mpi.WorldRank()

# Make Q a sparse semidefinite matrix
def Semidefinite(n):
  Q = El.DistSparseMatrix()
  Q.Resize(n,n)
  firstLocalRow = Q.FirstLocalRow()
  localHeight = Q.LocalHeight()
  Q.Reserve(localHeight)
  for sLoc in xrange(localHeight):
    s = firstLocalRow + sLoc
    Q.QueueLocalUpdate( sLoc, s, 1 );
  
  Q.MakeConsistent()
  return Q

# Make a sparse matrix with the last column dense
def Rectang(m,n):
  A = El.DistSparseMatrix()
  A.Resize(m,n)
  firstLocalRow = A.FirstLocalRow()
  localHeight = A.LocalHeight()
  A.Reserve(5*localHeight)
  for sLoc in xrange(localHeight):
    s = firstLocalRow + sLoc
    A.QueueLocalUpdate( sLoc, s, 11 )
    if s != 0:   A.QueueLocalUpdate( sLoc, s-1,   1 )
    if s != n-1: A.QueueLocalUpdate( sLoc, s+1,   2 )
    if s >= m:   A.QueueLocalUpdate( sLoc, s-m,   3 )
    if s <  n-m: A.QueueLocalUpdate( sLoc, s+m,   4 )
    # The dense last column
    A.QueueLocalUpdate( sLoc, n-1, 5./m );

  A.MakeConsistent()
  return A

Q = Semidefinite(n)
A = Rectang(m,n)

# Generate a right-hand side in the positive image
# ================================================
xGen = El.DistMultiVec()
El.Uniform(xGen,n,1,0.5,0.4999)
b = El.DistMultiVec()
El.Zeros( b, m, 1 )
El.SparseMultiply( El.NORMAL, 1., A, xGen, 0., b )

# Generate a random positive cost function
# ========================================
c = El.DistMultiVec()
El.Uniform(c,n,1,0.5,0.4999)

if display:
  El.Display( Q,    "Q"    )
  El.Display( A,    "A"    )
  El.Display( xGen, "xGen" )
  El.Display( b,    "b"    )
  El.Display( c,    "c"    )

# Generate random initial guesses
# ===============================
xOrig = El.DistMultiVec()
yOrig = El.DistMultiVec()
zOrig = El.DistMultiVec()
El.Uniform(xOrig,n,1,0.5,0.4999)
El.Uniform(yOrig,m,1,0.5,0.4999)
El.Uniform(zOrig,n,1,0.5,0.4999)
x = El.DistMultiVec()
y = El.DistMultiVec()
z = El.DistMultiVec()

if testMehrotra:
  El.Copy( xOrig, x )
  El.Copy( yOrig, y )
  El.Copy( zOrig, z )
  startMehrotra = time.clock()
  El.QPDirectMehrotra(Q,A,b,c,x,y,z)
  endMehrotra = time.clock()
  if worldRank == 0:
    print "Mehrotra time:", endMehrotra-startMehrotra

  if display:
    El.Display( x, "x Mehrotra" )
    El.Display( y, "y Mehrotra" )
    El.Display( z, "z Mehrotra" )

  Q_x = El.DistMultiVec()
  El.Zeros( Q_x, n, 1 )
  El.SparseMultiply( El.NORMAL, 1., Q, x, 0., Q_x )
  xTQx = El.Dot(x,Q_x)
  obj = El.Dot(c,x) + xTQx/2
  if worldRank == 0:
    print "Mehrotra primal objective =", obj

if testIPF:
  El.Copy( xOrig, x )
  El.Copy( yOrig, y )
  El.Copy( zOrig, z )
  startIPF = time.clock()
  El.QPDirectIPF(Q,A,b,c,x,y,z)
  endIPF = time.clock()
  if worldRank == 0:
    print "IPF time:", endIPF-startIPF

  if display:
    El.Display( x, "x IPF" )
    El.Display( y, "y IPF" )
    El.Display( z, "z IPF" )

  Q_x = El.DistMultiVec() 
  El.Zeros( Q_x, n, 1 )
  El.SparseMultiply( El.NORMAL, 1., Q, x, 0., Q_x )
  xTQx = El.Dot(x,Q_x)
  obj = El.Dot(c,x) + xTQx/2
  if worldRank == 0:
    print "IPF primal objective =", obj

# Require the user to press a button before the figures are closed
commSize = El.mpi.Size( El.mpi.COMM_WORLD() )
El.Finalize()
if commSize == 1:
  raw_input('Press Enter to exit')