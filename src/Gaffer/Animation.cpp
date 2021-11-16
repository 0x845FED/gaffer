//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2015, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//      * Redistributions of source code must retain the above
//        copyright notice, this list of conditions and the following
//        disclaimer.
//
//      * Redistributions in binary form must reproduce the above
//        copyright notice, this list of conditions and the following
//        disclaimer in the documentation and/or other materials provided with
//        the distribution.
//
//      * Neither the name of John Haddon nor the names of
//        any other contributors to this software may be used to endorse or
//        promote products derived from this software without specific prior
//        written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "Gaffer/Animation.h"

#include "Gaffer/Action.h"
#include "Gaffer/Context.h"

#include "OpenEXR/ImathFun.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>

namespace Gaffer
{

class Animation::Interpolator : public IECore::RefCounted
{
public:

	enum Hint
	{
		UseSlope = 1,
		UseScale = 2
	};

	Animation::Interpolation getInterpolation() const;
	unsigned getHints() const;

	static ConstInterpolatorPtr get( Animation::Interpolation interpolation );
	static ConstInterpolatorPtr getDefault();

protected:

	// construct with specified interpolation and hints
	explicit Interpolator( Animation::Interpolation interpolation, unsigned hints = 0 );

private:

	friend class CurvePlug;
	friend class Tangent;

	/// Implement to return interpolated value at specified normalised time
	virtual double evaluate( const Key& keyLo, const Key& keyHi, double time, double dt ) const = 0;

	/// Implement to compute the effective slope of the specified tangent
	virtual double effectiveSlope( const Tangent& tangent, double dt, double dv ) const;

	/// Implement to compute the effective scale of the specified tangent
	virtual double effectiveScale( const Tangent& tangent, double dt, double dv ) const;

	typedef std::vector< ConstInterpolatorPtr > Container;
	static const Container& get();

	Animation::Interpolation m_interpolation;
	unsigned m_hints;
};

} // Gaffer

namespace
{

double maxScale( const double slope )
{
	// NOTE : s = y/x
	//        l = sqrt(x^2 + y^2)
	//
	//        When scale at maximum, x = 1, therefore,
	//
	//        y = s
	//        l = sqrt(1 + s^2)

	return std::sqrt( std::fma( slope, slope, 1.0 ) );
}

double slopeFromPosition( const Imath::V2d& position, const Gaffer::Animation::Direction direction )
{
	static_assert( std::numeric_limits< double >::is_iec559, "IEEE 754 required to represent negative infinity" );

	// NOTE : when x and y are both 0 then slope is 0, otherwise if x is 0 slope is (+/-) infinity

	if( position.x == 0.0 )
	{
		if( position.y == 0.0 )
		{
			return 0.0;
		}

		return std::copysign( std::numeric_limits< double >::infinity(),
			position.y * ( direction == Gaffer::Animation::Direction::In ? -1.0 : 1.0 ) );
	}
	else
	{
		return position.y / position.x;
	}
}

// constant interpolator

struct InterpolatorConstant
: public Gaffer::Animation::Interpolator
{
	InterpolatorConstant()
	: Gaffer::Animation::Interpolator( Gaffer::Animation::Interpolation::Constant )
	{}

	double evaluate( const Gaffer::Animation::Key& keyLo, const Gaffer::Animation::Key& /*keyHi*/,
		const double /*time*/, const double /*dt*/ ) const override
	{
		return keyLo.getValue();
	}
};

// constant next interpolator

struct InterpolatorConstantNext
: public Gaffer::Animation::Interpolator
{
	InterpolatorConstantNext()
	: Gaffer::Animation::Interpolator( Gaffer::Animation::Interpolation::ConstantNext )
	{}

	double evaluate( const Gaffer::Animation::Key& /*keyLo*/, const Gaffer::Animation::Key& keyHi,
		const double /*time*/, const double /*dt*/ ) const override
	{
		return keyHi.getValue();
	}
};

// linear interpolator

struct InterpolatorLinear
: public Gaffer::Animation::Interpolator
{
	InterpolatorLinear()
	: Gaffer::Animation::Interpolator( Gaffer::Animation::Interpolation::Linear )
	{}

	double evaluate( const Gaffer::Animation::Key& keyLo, const Gaffer::Animation::Key& keyHi,
		const double time, const double /*dt*/ ) const override
	{
		return keyLo.getValue() * ( 1.0 - time ) + keyHi.getValue() * ( time );
	}

	double effectiveSlope( const Gaffer::Animation::Tangent& /*tangent*/, const double dt, const double dv ) const override
	{
		return ( dv / dt );
	}
};

// cubic interpolator

struct InterpolatorCubic
: public Gaffer::Animation::Interpolator
{
	InterpolatorCubic()
	: Gaffer::Animation::Interpolator( Gaffer::Animation::Interpolation::Cubic,
		Gaffer::Animation::Interpolator::Hint::UseSlope )
	{}

	double evaluate( const Gaffer::Animation::Key& keyLo, const Gaffer::Animation::Key& keyHi,
		const double time, const double dt ) const override
	{
		double a, b, c, d;
		computeCoeffs( keyLo, keyHi, a, b, c, d, dt );

		// NOTE : v  = at^3 + bt^2 + ct + d

		return std::fma( time, std::fma( time, std::fma( time, a, b ), c ), d );
	}

	double effectiveScale( const Gaffer::Animation::Tangent& tangent, const double dt, const double /*dv*/ ) const override
	{
		return ( 1.0 / 3.0 ) * maxScale( clampSlope( tangent.getSlope() * dt ) / dt );
	}

private:

	static double clampSlope( const double slope )
	{
		const double maxSlope = 1.e9;
		return Imath::clamp( slope, -maxSlope, maxSlope );
	}

	void computeCoeffs(
		const Gaffer::Animation::Key& keyLo, const Gaffer::Animation::Key& keyHi,
		double& a, double& b, double& c, double& d, const double dt ) const
	{
		// NOTE : clamp slope to prevent infs and nans in interpolated values

		const double dv = keyHi.getValue() - keyLo.getValue();
		const double sl = clampSlope( keyLo.tangentOut().getSlope() * dt );
		const double sh = clampSlope( keyHi.tangentIn().getSlope() * dt );

		a = sl + sh - dv - dv;
		b = dv + dv + dv - sl - sl - sh;
		c = sl;
		d = keyLo.getValue();
	}
};

// bezier interpolator

struct InterpolatorBezier
: public Gaffer::Animation::Interpolator
{
	InterpolatorBezier()
	: Gaffer::Animation::Interpolator( Gaffer::Animation::Interpolation::Bezier,
		Gaffer::Animation::Interpolator::Hint::UseSlope |
		Gaffer::Animation::Interpolator::Hint::UseScale )
	{}

	double evaluate( const Gaffer::Animation::Key& keyLo, const Gaffer::Animation::Key& keyHi,
		const double time, const double dt ) const override
	{
		const Imath::V2d tl = keyLo.tangentOut().getPosition();
		const Imath::V2d th = keyHi.tangentIn().getPosition();

		// NOTE : Curve is determined by two polynomials parameterised by s,
		//
		//        v = a(v)s^3 +  b(v)s^2 + c(v)s + d(v)
		//        t = a(t)s^3 +  b(t)s^2 + c(t)s + d(t)
		//
		//        where t is normalised time in seconds, v is value, to evaluate v at the
		//        specified t, first need to solve the second polynomial to determine s.

		const double s = solveForTime(
			Imath::clamp( ( tl.x - keyLo.getTime() ) / dt,       0.0, 1.0 ),
			Imath::clamp( ( th.x - keyHi.getTime() ) / dt + 1.0, 0.0, 1.0 ), time );

		// compute coefficients of value polynomial

		const double valueLo = keyLo.getValue();
		const double valueHi = keyHi.getValue();
		const double tl3 = tl.y + tl.y + tl.y;
		const double th3 = th.y + th.y + th.y;
		const double vl3 = valueLo + valueLo + valueLo;
		const double av = tl3 - th3 + valueHi - valueLo;
		const double bv = th3 + vl3 - tl3 - tl3;
		const double cv = tl3 - vl3;
		const double dv = valueLo;

		// evaluate value polynomial

		return std::fma( s, std::fma( s, std::fma( s, av, bv ), cv ), dv );
	}

private:

	double solveForTime( const double tl, const double th, const double time ) const
	{
		if( time <= 0.0 ) return 0.0;
		if( time >= 1.0 ) return 1.0;

		// compute coeffs

		const double th3 = th + th + th;
		const double ct = tl + tl + tl;
		const double at = ct - th3 + 1.0;
		const double bt = th3 - ct - ct;
		const double bt2 = bt + bt;
		const double at3 = at + at + at;

		// check that f is monotonic and therefore has one (possibly repeated) real root.
		//
		// NOTE : f is monotonic over the interval [0,1] when the solutions of f' either,
		//        both lie outside the interval (0,1) or lie in the interval (0,1) and are equal
		//        in which case the discriminant of f' is zero.
		// NOTE : keeping tl and th in the range [0,1] ensures f is monotonic over interval [0,1].

		const double discriminant = bt2 * bt2 - 4.0 * at3 * ct;

		if( discriminant > 1e-13 )
		{
			const double q = - 0.5 * ( bt2 + std::copysign( std::sqrt( discriminant ), bt2 ) );
			const double s1 = q / at3;
			const double s2 = ct / q;

			if( ( 0.0 < s1 && s1 < 1.0 ) || ( 0.0 < s2 && s2 < 1.0 ) )
			{
				throw IECore::Exception( "Animation : Bezier interpolation mode : curve segment has multiple values for given time." );
			}
		}

		// root bracketed in interval [0,1]

		double sl = 0.0;
		double sh = 1.0;

		// time is a reasonable first guess

		double s = time;

		// max of 10 newton-raphson iterations

		for( int i = 0; i < 10; ++i )
		{
			// evaluate function and derivative
			//
			// NOTE : f   =  a(t)s^3 +  b(t)s^2 + c(t)s + d(t) - t
			//        f'  = 3a(t)s^2 + 2b(t)s   + c(t)

			const double  f = std::fma( s, std::fma( s, std::fma( s, at,  bt  ), ct ), -time );
			const double df =              std::fma( s, std::fma( s, at3, bt2 ), ct );

			// maintain bounds

			if( std::abs( f ) < std::numeric_limits< double >::epsilon() )
			{
				break;
			}
			else if( f < 0.0 )
			{
				sl = s;
			}
			else
			{
				sh = s;
			}

			// NOTE : when derivative is zero or newton-raphson step would escape bounds use bisection step instead.

			double ds;

			if( df == 0.0 )
			{
				ds = 0.5 * ( sh - sl );
				s = sl + ds;
			}
			else
			{
				ds = f / df;

				if( ( ( s - ds ) <= sl ) || ( ( s - ds ) >= sh ) )
				{
					ds = 0.5 * ( sh - sl );
					s = sl + ds;
				}
				else
				{
					s -= ds;
				}
			}

			assert( s >= sl );
			assert( s <= sh );

			// check for convergence

			if( std::abs( ds ) < std::numeric_limits< double >::epsilon() )
			{
				break;
			}
		}

		return s;
	}
};

} // namespace

namespace Gaffer
{

//////////////////////////////////////////////////////////////////////////
// Interpolator implementation
//////////////////////////////////////////////////////////////////////////

Animation::Interpolator::Interpolator( const Animation::Interpolation interpolation, const unsigned hints )
: m_interpolation( interpolation )
, m_hints( hints )
{}

Animation::Interpolation Animation::Interpolator::getInterpolation() const
{
	return m_interpolation;
}

unsigned Animation::Interpolator::getHints() const
{
	return m_hints;
}

double Animation::Interpolator::evaluate(
	const Animation::Key& /*keyLo*/, const Animation::Key& /*keyHi*/,
	const double /*time*/, const double /*dt*/ ) const
{
	return 0.0;
}

double Animation::Interpolator::effectiveSlope(
	const Animation::Tangent& /*tangent*/, const double /*dt*/, const double /*dv*/ ) const
{
	return 0.0;
}

double Animation::Interpolator::effectiveScale(
	const Animation::Tangent& /*tangent*/, const double /*dt*/, const double /*dv*/ ) const
{
	return 0.0;
}

const Animation::Interpolator::Container& Animation::Interpolator::get()
{
	static const Container container
	{
		ConstInterpolatorPtr( new InterpolatorBezier() ),
		ConstInterpolatorPtr( new InterpolatorCubic() ),
		ConstInterpolatorPtr( new InterpolatorLinear() ),
		ConstInterpolatorPtr( new InterpolatorConstantNext() ),
		ConstInterpolatorPtr( new InterpolatorConstant() )
	};

	return container;
}

Animation::ConstInterpolatorPtr Animation::Interpolator::get( const Animation::Interpolation interpolation )
{
	const Container& container = Interpolator::get();
	const Container::const_iterator it =
		std::find_if( container.begin(), container.end(),
			[ interpolation ]( const ConstInterpolatorPtr& interpolator ) -> bool
			{ return interpolator->getInterpolation() == interpolation; } );
	return ( it != container.end() ) ? ( *it ) : Interpolator::getDefault();
}

Animation::ConstInterpolatorPtr Animation::Interpolator::getDefault()
{
	return Interpolator::get().front();
}

//////////////////////////////////////////////////////////////////////////
// Tangent implementation
//////////////////////////////////////////////////////////////////////////

Animation::Tangent::Tangent( Animation::Key& key, const Animation::Direction direction, const double slope, const double scale )
: m_key( & key )
, m_direction( direction )
, m_dt( 0.0 )
, m_dv( 0.0 )
, m_slope( slope )
, m_scale( Imath::clamp( scale, 0.0, maxScale( m_slope ) ) )
{}

Animation::Tangent::~Tangent()
{}

Animation::Key& Animation::Tangent::key()
{
	assert( m_key );
	return *m_key;
}

const Animation::Key& Animation::Tangent::key() const
{
	assert( m_key );
	return *m_key;
}

Animation::Direction Animation::Tangent::direction() const
{
	return m_direction;
}

void Animation::Tangent::setSlope( const double slope )
{
	setSlope( slope, false );
}

void Animation::Tangent::setSlope( double slope, const bool force )
{
	// check that slope is unconstrained

	if( ! force && slopeIsConstrained() )
	{
		return;
	}

	// check for no change

	if( m_slope == slope )
	{
		return;
	}

	// clamp existing scale based on new slope

	const double scale = std::min( m_scale, maxScale( slope ) );

	// make change via action

	if( m_key->m_parent )
	{
		KeyPtr key = m_key;
		const double previousSlope = m_slope;
		const double previousScale = m_scale;
		Action::enact(
			key->m_parent,
			// Do
			[ this, key, slope, scale ] {
				m_slope = slope;
				m_scale = scale;
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			},
			// Undo
			[ this, key, previousSlope, previousScale ] {
				m_slope = previousSlope;
				m_scale = previousScale;
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			}
		);
	}
	else
	{
		m_slope = slope;
		m_scale = scale;
	}
}

double Animation::Tangent::getSlope() const
{
	if( slopeIsConstrained() )
	{
		return ( ( m_direction == Direction::Out )
			? m_key->m_interpolator
			: m_key->prevKey()->m_interpolator )->effectiveSlope( *this, m_dt, m_dv );
	}

	return m_slope;
}

bool Animation::Tangent::slopeIsConstrained() const
{
	assert( m_key );

	// when unparented or inactive slope is not constrained

	if( ! m_key->m_parent || ! m_key->m_active )
	{
		return false;
	}

	// check interpolator hints

	if(
		( ( m_direction == Direction::Out ) && ( m_key->m_parent->finalKey() != m_key ) &&
			! ( m_key->m_interpolator->getHints() & Interpolator::Hint::UseSlope ) ) ||
		( ( m_direction == Direction::In ) && ( m_key->m_parent->firstKey() != m_key ) &&
			! ( m_key->prevKey()->m_interpolator->getHints() & Interpolator::Hint::UseSlope ) ) )
	{
		return true;
	}

	return false;
}

void Animation::Tangent::setScale( double scale )
{
	setScale( scale, false );
}

void Animation::Tangent::setScale( double scale, const bool force )
{
	// check that scale is unconstrained

	if( ! force && scaleIsConstrained() )
	{
		return;
	}

	// clamp new scale based on existing slope

	scale = Imath::clamp( scale, 0.0, maxScale( m_slope ) );

	// check for no change

	if( m_scale == scale )
	{
		return;
	}

	// make change via action

	if( m_key->m_parent )
	{
		KeyPtr key = m_key;
		const double previousScale = m_scale;
		Action::enact(
			key->m_parent,
			// Do
			[ this, key, scale ] {
				m_scale = scale;
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			},
			// Undo
			[ this, key, previousScale ] {
				m_scale = previousScale;
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			}
		);
	}
	else
	{
		m_scale = scale;
	}
}

double Animation::Tangent::getScale() const
{
	if( scaleIsConstrained() )
	{
		return ( ( m_direction == Direction::Out )
			? m_key->m_interpolator
			: m_key->prevKey()->m_interpolator )->effectiveScale( *this, m_dt, m_dv );
	}

	return m_scale;
}

bool Animation::Tangent::scaleIsConstrained() const
{
	assert( m_key );

	// when unparented or inactive scale is not constrained

	if( ! m_key->m_parent || ! m_key->m_active )
	{
		return false;
	}

	// check interpolator hints

	if(
		( ( m_direction == Direction::Out ) && ( m_key->m_parent->finalKey() != m_key ) &&
			! ( m_key->m_interpolator->getHints() & Interpolator::Hint::UseScale ) ) ||
		( ( m_direction == Direction::In ) && ( m_key->m_parent->firstKey() != m_key ) &&
			! ( m_key->prevKey()->m_interpolator->getHints() & Interpolator::Hint::UseScale ) ) )
	{
		return true;
	}

	return false;
}

void Animation::Tangent::setPosition( const Imath::V2d& pos, const bool relative )
{
	// when span width is zero position is constrained to parent key

	if( m_dt == 0.0 )
	{
		return;
	}

	// convert relative position

	Imath::V2d position( pos );
	positionToRelative( position, relative );

	// set slope and scale

	setSlope( slopeFromPosition( position, m_direction ) );
	setScale( position.length() / m_dt );
}

Imath::V2d Animation::Tangent::getPosition( const bool relative ) const
{
	Imath::V2d p( 0.0, 0.0 );

	// when span width is zero position is that of parent key

	if( m_dt != 0.0 )
	{
		// compute relative position
		//
		// NOTE : s   = y/x
		//            = tan(angle)
		//        x   = l * cos(angle)
		//            = l / sqrt(1 + tan^2(angle))
		//            = l / sqrt(1 + s^2)
		//        y   = x * s
		//
		//        1/s = x/y
		//            = tan(PI/2-angle)
		//        y   = l * cos(PI/2-angle)
		//            = l / sqrt(1 + tan^2(PI/2-angle))
		//            = l / sqrt(1 + (1/s)^2)
		//        x   = y * (1/s)
		//
		//        As s tends to 0, sqrt(1 + s^2) tends to 1, so x tends to l and y tends to 0, but
		//        as s tends to (+/-) infinity, sqrt(1 + s^2) tends to infinity, so x tends to 0
		//        and y becomes meaningless. However as s tends to (+/-) infinity, 1/s tends to 0
		//        so sqrt(1 + (1/s)^2) tends to 1, so y tends to l and x tends to 0. So,
		//
		//            when |s| <  1 : x = l / sqrt(1 + s^2)
		//                            y = x * s
		//            when |s| >= 1 : y = l / sqrt(1 + (1/s)^2)
		//                            x = y * (1/s)

		const double slope = getSlope();
		const double scale = getScale();

		if( std::abs( slope ) < 1.0 )
		{
			const double s = slope;
			p.x = std::min( ( scale * m_dt ) / std::sqrt( std::fma( s, s, 1.0 ) ), m_dt );
			p.y = p.x * s;
		}
		else
		{
			const double s = 1.0 / slope;
			p.y = std::copysign( ( scale * m_dt ) / std::sqrt( std::fma( s, s, 1.0 ) ), s );
			p.x = std::min( p.y * s, m_dt );
		}

		if( m_direction == Direction::In )
		{
			if( p.x != 0.0 ) { p.x = -p.x; }
			if( p.y != 0.0 ) { p.y = -p.y; }
		}
	}

	// convert to absolute position

	if( ! relative )
	{
		p.x += m_key->m_time;
		p.y += m_key->m_value;
	}

	return p;
}

void Animation::Tangent::update()
{
	assert( m_key );

	// update span time and value differences

	double dt = 0.0;
	double dv = 0.0;

	if( m_key->m_parent )
	{
		switch( m_direction )
		{
			case Direction::In:
				if( const Key* const kp = m_key->prevKey() )
				{
					dt = ( m_key->m_time - kp->m_time );
					dv = ( m_key->m_value - kp->m_value );
				}
				break;
			case Direction::Out:
				if( const Key* const kn = m_key->nextKey() )
				{
					dt = ( kn->m_time - m_key->m_time );
					dv = ( kn->m_value - m_key->m_value );
				}
				break;
			default:
				break;
		}
	}

	// NOTE : when dt becomes zero either the tangent's parent key has been removed from a curve
	//        or the tangent's direction is in and its parent key is the first key in a curve
	//        or the tangent's direction is out and its parent key is the final key in a curve.
	// NOTE : when dt becomes non zero either the tangent's parent key has been added to a curve
	//        or is no longer the first or final key in a curve.

	m_dv = dv;
	m_dt = dt;
}

void Animation::Tangent::positionToRelative( Imath::V2d& position, const bool relative ) const
{
	assert( m_dt != 0.0 );

	// convert from absolute position

	if( ! relative )
	{
		position.x -= m_key->m_time;
		position.y -= m_key->m_value;
	}

	// constrain direction of tangent

	position.x = ( m_direction == Direction::In )
		? std::min( position.x, 0.0 )
		: std::max( position.x, 0.0 );
}

//////////////////////////////////////////////////////////////////////////
// Key implementation
//////////////////////////////////////////////////////////////////////////

IE_CORE_DEFINERUNTIMETYPED( Gaffer::Animation::Key )

Animation::Key::Key( const float time, const float value, const Animation::Interpolation interpolation,
	const double inSlope, const double inScale, const double outSlope, const double outScale )
: m_parent( nullptr )
, m_in( *this, Direction::In, inSlope, inScale )
, m_out( *this, Direction::Out, outSlope, outScale )
, m_time( time )
, m_value( value )
, m_interpolator( Interpolator::get( interpolation ) )
, m_active( false )
{}

Animation::Key::~Key()
{
	// NOTE : parent reference should have been reset before the key is destructed

	assert( m_parent == nullptr );
}

Animation::Tangent& Animation::Key::tangentIn()
{
	return m_in;
}

const Animation::Tangent& Animation::Key::tangentIn() const
{
	return m_in;
}

Animation::Tangent& Animation::Key::tangentOut()
{
	return m_out;
}

const Animation::Tangent& Animation::Key::tangentOut() const
{
	return m_out;
}

Animation::Tangent& Animation::Key::tangent( const Animation::Direction direction )
{
	return const_cast< Tangent& >(
		static_cast< const Key* >( this )->tangent( direction ) );
}

const Animation::Tangent& Animation::Key::tangent( const Animation::Direction direction ) const
{
	return ( direction == Direction::In ) ? m_in : m_out;
}

float Animation::Key::getTime() const
{
	return m_time;
}

Animation::KeyPtr Animation::Key::setTime( const float time )
{
	if( time == m_time )
	{
		return KeyPtr();
	}

	KeyPtr clashingKey;

	if( m_parent )
	{
		// find any clashing active key.
		clashingKey = m_parent->getKey( time );

		// if key is active find first clashing inactive key
		KeyPtr clashingInactiveKey;
		if( m_active )
		{
			const CurvePlug::InactiveKeys::iterator it = m_parent->m_inactiveKeys.find( m_time );
			if( it != m_parent->m_inactiveKeys.end() )
			{
				clashingInactiveKey = &( *it );
			}
		}

		// time change via action

		KeyPtr key = this;
		const float previousTime = m_time;
		const bool active = m_active;
		CurvePlug* const curve = m_parent;

#		define ASSERTCONTAINSKEY( KEY, CONTAINER, RESULT ) \
			assert( ( RESULT ) == ( std::find_if( ( CONTAINER ).begin(), ( CONTAINER ).end(), \
				[ key = &( *( KEY ) ) ]( const Key& k ) { return key == & k; } ) != ( CONTAINER ).end() ) );

		Action::enact(
			m_parent,
			// Do
			[ curve, time, previousTime, active, key, clashingKey, clashingInactiveKey ] {
				// check state is as expected
				key->throwIfStateNotAsExpected( curve, active, previousTime );
				if( clashingInactiveKey )
					clashingInactiveKey->throwIfStateNotAsExpected( curve, false, previousTime );
				if( clashingKey )
					clashingKey->throwIfStateNotAsExpected( curve, true, time );
				// NOTE : If key is inactive,
				//          remove key from inactive keys container
				//        else if there is a clashing inactive key
				//          remove the clashing inactive key from inactive keys container
				//          replace key with clashing inactive key in active keys container
				//        else
				//          remove key from active keys container
				//        set time of key
				//        If there is a clashing active key,
				//          replace clashing active key with key in active container
				//          insert clashing key into inactive keys container
				//        else insert key into active keys container
				// NOTE : It is critical to the following code that the key comparison NEVER throws.
				Key* const kpn = key->nextKey();
				Key* const kpp = key->prevKey();
				assert( key->m_hook.is_linked() );
				if( ! active )
				{
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, true )
					curve->m_inactiveKeys.erase( curve->m_inactiveKeys.iterator_to( *key ) );
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, false )
				}
				else if( clashingInactiveKey )
				{
					assert( clashingInactiveKey->m_hook.is_linked() );
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_inactiveKeys, true )
					curve->m_inactiveKeys.erase( curve->m_inactiveKeys.iterator_to( *clashingInactiveKey ) );
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_inactiveKeys, false )
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					curve->m_keys.replace_node( curve->m_keys.iterator_to( *key ), *clashingInactiveKey );
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_keys, true )
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					clashingInactiveKey->m_active = true;
				}
				else
				{
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					curve->m_keys.erase( curve->m_keys.iterator_to( *key ) );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
				}
				assert( ! key->m_hook.is_linked() );
				key->m_time = time;
				key->m_active = true;
				if( clashingKey )
				{
					assert( clashingKey->m_hook.is_linked() );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, false )
					ASSERTCONTAINSKEY( clashingKey, curve->m_keys, true )
					curve->m_keys.replace_node( curve->m_keys.iterator_to( *clashingKey ), *key );
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					ASSERTCONTAINSKEY( clashingKey, curve->m_keys, false )
					assert( ! clashingKey->m_hook.is_linked() );
					curve->m_inactiveKeys.insert_before( curve->m_inactiveKeys.lower_bound( key->m_time ), *clashingKey );
					ASSERTCONTAINSKEY( clashingKey, curve->m_inactiveKeys, true )
					clashingKey->m_active = false;
				}
				else
				{
					assert( curve->m_keys.count( key->m_time ) == static_cast< CurvePlug::Keys::size_type >( 0 ) );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					curve->m_keys.insert( *key );
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
				}

				assert( ! key || ( key->m_active == true ) );
				assert( ! clashingKey || ( clashingKey->m_active == false ) );
				assert( ! clashingInactiveKey || ( clashingInactiveKey->m_active == true ) );

				// update keys
				//
				// NOTE : only update next/prev keys at old time when there is no clashing inactive
				//        key as any clashing inactive key will replace the key whose time is being
				//        set. only update the next/prev keys at the new time when there is no active
				//        clashing key as the key whose time is being set will replace any active
				//        clashing key. The key and any clashing inactive key are always updated.
				key->m_out.update();
				key->m_in.update();
				if( clashingInactiveKey )
				{
					clashingInactiveKey->m_in.update();
					clashingInactiveKey->m_out.update();
				}
				else
				{
					if( kpn ){ kpn->m_in.update(); }
					if( kpp ){ kpp->m_out.update(); }
				}
				if( ! clashingKey )
				{
					Key* const kn = key->nextKey();
					if( kn && ( kn != kpn || clashingInactiveKey ) ){ kn->m_in.update(); }
					Key* const kp = key->prevKey();
					if( kp && ( kp != kpp || clashingInactiveKey ) ){ kp->m_out.update(); }
				}

				curve->m_keyTimeChangedSignal( key->m_parent, key.get() );
				curve->propagateDirtiness( curve->outPlug() );
			},
			// Undo
			[ curve, time, previousTime, active, key, clashingKey, clashingInactiveKey ] {
				// check state is as expected
				key->throwIfStateNotAsExpected( curve, true, time );
				if( clashingKey )
					clashingKey->throwIfStateNotAsExpected( curve, false, time );
				if( clashingInactiveKey )
					clashingInactiveKey->throwIfStateNotAsExpected( curve, true, previousTime );
				// NOTE : If there was a clashing active key
				//          remove the clashing active key from inactive keys container
				//          replace key with clashing active key in active keys container
				//        else
				//          remove key from active keys container
				//        reset time of key
				//        If key was inactive reinsert key into inactive container
				//        else if there was a clashing inactive key
				//          replace clashing inactive key with key in active container
				//          reinsert clashing inactive key into inactive keys container
				//        else reinsert key into active keys container
				// NOTE : It is critical to the following code that the key comparison NEVER throws.
				Key* const kpn = key->nextKey();
				Key* const kpp = key->prevKey();
				assert( key->m_hook.is_linked() );
				if( clashingKey )
				{
					assert( clashingKey->m_hook.is_linked() );
					ASSERTCONTAINSKEY( clashingKey, curve->m_inactiveKeys, true )
					curve->m_inactiveKeys.erase( curve->m_inactiveKeys.iterator_to( *clashingKey ) );
					ASSERTCONTAINSKEY( clashingKey, curve->m_inactiveKeys, false )
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					curve->m_keys.replace_node( curve->m_keys.iterator_to( *key ), *clashingKey );
					ASSERTCONTAINSKEY( clashingKey, curve->m_keys, true )
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					clashingKey->m_active = true;
				}
				else
				{
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					curve->m_keys.erase( curve->m_keys.iterator_to( *key ) );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
				}
				assert( ! key->m_hook.is_linked() );
				key->m_time = previousTime;
				key->m_active = active;
				if( ! key->m_active )
				{
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, false )
					curve->m_inactiveKeys.insert_before( curve->m_inactiveKeys.lower_bound( key->m_time ), *key );
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, true )
				}
				else if( clashingInactiveKey )
				{
					assert( clashingInactiveKey->m_hook.is_linked() );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					ASSERTCONTAINSKEY( key, curve->m_inactiveKeys, false )
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_keys, true )
					curve->m_keys.replace_node( curve->m_keys.iterator_to( *clashingInactiveKey ), *key );
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_keys, false )
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_inactiveKeys, false )
					curve->m_inactiveKeys.insert_before( curve->m_inactiveKeys.lower_bound( key->m_time ), *clashingInactiveKey );
					ASSERTCONTAINSKEY( clashingInactiveKey, curve->m_inactiveKeys, true )
					clashingInactiveKey->m_active = false;
				}
				else
				{
					assert( curve->m_keys.count( key->m_time ) == static_cast< CurvePlug::Keys::size_type >( 0 ) );
					ASSERTCONTAINSKEY( key, curve->m_keys, false )
					curve->m_keys.insert( *key );
					ASSERTCONTAINSKEY( key, curve->m_keys, true )
				}

				assert( ! key || ( key->m_active == active ) );
				assert( ! clashingKey || ( clashingKey->m_active == true ) );
				assert( ! clashingInactiveKey || ( clashingInactiveKey->m_active == false ) );

				// update keys
				//
				// NOTE : only update next/prev keys at old time when there is no clashing inactive
				//        key as key whose time is being set replaces any clashing inactive key.
				//        only update the next/prev keys at the new time when there is no active
				//        clashing key as any active clashing key will replace the key whose time
				//        is being set. The active clashing key is updated as it becomes active.
				//        The key whose time was set is updated if it was active.
				if( active )
				{
					key->m_in.update();
					key->m_out.update();
				}
				if( clashingKey )
				{
					clashingKey->m_in.update();
					clashingKey->m_out.update();
				}
				else
				{
					if( kpn ){ kpn->m_in.update(); }
					if( kpp ){ kpp->m_out.update(); }
				}
				if( ! clashingInactiveKey )
				{
					Key* const kn = key->nextKey();
					if( kn && ( kn != kpn || clashingKey ) ){ kn->m_in.update(); }
					Key* const kp = key->prevKey();
					if( kp && ( kp != kpp || clashingKey ) ){ kp->m_out.update(); }
				}

				curve->m_keyTimeChangedSignal( key->m_parent, key.get() );
				curve->propagateDirtiness( curve->outPlug() );
			}
		);

#		undef ASSERTCONTAINSKEY
	}
	else
	{
		m_time = time;
	}

	return clashingKey;
}

float Animation::Key::getValue() const
{
	return m_value;
}

void Animation::Key::setValue( const float value )
{
	if( value == m_value )
	{
		return;
	}

	// NOTE : inactive keys remain parented and participate in undo/redo and signalling

	if( m_parent )
	{
		KeyPtr key = this;
		const float previousValue = m_value;
		Action::enact(
			m_parent,
			// Do
			[ key, value ] {
				key->m_value = value;
				key->m_out.update();
				key->m_in.update();
				if( Key* const kn = key->nextKey() ){ kn->m_in.update(); }
				if( Key* const kp = key->prevKey() ){ kp->m_out.update(); }
				key->m_parent->m_keyValueChangedSignal( key->m_parent, key.get() );
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			},
			// Undo
			[ key, previousValue ] {
				key->m_value = previousValue;
				key->m_out.update();
				key->m_in.update();
				if( Key* const kn = key->nextKey() ){ kn->m_in.update(); }
				if( Key* const kp = key->prevKey() ){ kp->m_out.update(); }
				key->m_parent->m_keyValueChangedSignal( key->m_parent, key.get() );
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			}
		);
	}
	else
	{
		m_value = value;
	}
}

Animation::Interpolation Animation::Key::getInterpolation() const
{
	return m_interpolator->getInterpolation();
}

void Animation::Key::setInterpolation( const Animation::Interpolation interpolation )
{
	const ConstInterpolatorPtr interpolator = Interpolator::get( interpolation );

	if( ! interpolator || ( interpolator == m_interpolator ) )
	{
		return;
	}

	// NOTE : inactive keys remain parented and participate in undo/redo and signalling

	if( m_parent )
	{
		KeyPtr key = this;
		const ConstInterpolatorPtr previousInterpolator = m_interpolator;
		Action::enact(
			m_parent,
			// Do
			[ key, interpolator ] {
				key->m_interpolator = interpolator;
				key->m_parent->m_keyInterpolationChangedSignal( key->m_parent, key.get() );
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			},
			// Undo
			[ key, previousInterpolator ] {
				key->m_interpolator = previousInterpolator;
				key->m_parent->m_keyInterpolationChangedSignal( key->m_parent, key.get() );
				key->m_parent->propagateDirtiness( key->m_parent->outPlug() );
			}
		);
	}
	else
	{
		m_interpolator = interpolator;
	}
}

bool Animation::Key::isActive() const
{
	return m_active;
}

Animation::Key *Animation::Key::nextKey()
{
	return const_cast< Key* >( static_cast< const Key* >( this )->nextKey() );
}

Animation::Key *Animation::Key::prevKey()
{
	return const_cast< Key* >( static_cast< const Key* >( this )->prevKey() );
}

const Animation::Key *Animation::Key::nextKey() const
{
	const Key* k = 0;

	if( m_parent && m_active )
	{
		assert( m_hook.is_linked() );

		CurvePlug::Keys::const_iterator it = m_parent->m_keys.iterator_to( *this );

		if( ++it != m_parent->m_keys.end() )
		{
			k = &( *it );
		}
	}

	return k;
}

const Animation::Key *Animation::Key::prevKey() const
{
	const Key* k = 0;

	if( m_parent && m_active )
	{
		CurvePlug::Keys::const_iterator it = m_parent->m_keys.iterator_to( *this );

		if( it-- != m_parent->m_keys.begin() )
		{
			k = &( *it );
		}
	}

	return k;
}

Animation::CurvePlug *Animation::Key::parent()
{
	return m_parent;
}

const Animation::CurvePlug *Animation::Key::parent() const
{
	return m_parent;
}

void Animation::Key::throwIfStateNotAsExpected( const Animation::CurvePlug* const curve, const bool active, const float time ) const
{
	// check that state is as expected
	//
	// NOTE : state may be changed outside the undo system and therefore not be as expected in
	//        which case throw an appropriate exception so user is informed of invalid api usage.

	if( m_parent != curve )
	{
		throw IECore::Exception( "Key parent changed outside undo system." );
	}

	if( m_active != active )
	{
		throw IECore::Exception( "Key active changed outside undo system." );
	}

	if( m_time != time )
	{
		throw IECore::Exception( "Key time changed outside undo system." );
	}
}

void Animation::Key::Dispose::operator()( Animation::Key* const key ) const
{
	assert( key != nullptr );

	key->m_parent = nullptr;
	key->m_active = false;
	key->removeRef();
}

//////////////////////////////////////////////////////////////////////////
// CurvePlug implementation
//////////////////////////////////////////////////////////////////////////

GAFFER_PLUG_DEFINE_TYPE( Animation::CurvePlug );

Animation::CurvePlug::CurvePlug( const std::string &name, const Plug::Direction direction, const unsigned flags )
: ValuePlug( name, direction, flags & ~Plug::AcceptsInputs )
, m_keys()
, m_inactiveKeys()
, m_keyAddedSignal()
, m_keyRemovedSignal()
, m_keyTimeChangedSignal()
, m_keyValueChangedSignal()
, m_keyInterpolationChangedSignal()
{
	addChild( new FloatPlug( "out", Plug::Out ) );
}

Animation::CurvePlug::~CurvePlug()
{
	m_keys.clear_and_dispose( Key::Dispose() );
	m_inactiveKeys.clear_and_dispose( Key::Dispose() );
}

Animation::CurvePlug::CurvePlugKeySignal& Animation::CurvePlug::keyAddedSignal()
{
	return m_keyAddedSignal;
}

Animation::CurvePlug::CurvePlugKeySignal& Animation::CurvePlug::keyRemovedSignal()
{
	return m_keyRemovedSignal;
}

Animation::CurvePlug::CurvePlugKeySignal& Animation::CurvePlug::keyTimeChangedSignal()
{
	return m_keyTimeChangedSignal;
}

Animation::CurvePlug::CurvePlugKeySignal& Animation::CurvePlug::keyValueChangedSignal()
{
	return m_keyValueChangedSignal;
}

Animation::CurvePlug::CurvePlugKeySignal& Animation::CurvePlug::keyInterpolationChangedSignal()
{
	return m_keyInterpolationChangedSignal;
}

Animation::KeyPtr Animation::CurvePlug::addKey( const Animation::KeyPtr &key, const bool removeActiveClashing )
{
	const KeyPtr clashingKey = getKey( key->m_time );

	if( clashingKey )
	{
		if( key == clashingKey )
		{
			return KeyPtr();
		}
	}

	if( key->m_parent )
	{
		key->m_parent->removeKey( key.get() );
	}

	// save the time of the key at the point it is added in case it was previously
	// removed from the curve and changes have been made whilst the key was outside
	// the curve (these changes will not have been recorded in the undo/redo system)
	// when redo is called we can then check for any change and throw an exception
	// if time is not as we expect it to be. principle here is that the user should
	// not make changes outside the undo system so if they have then let them know.

	const float time = key->m_time;

#	define ASSERTCONTAINSKEY( KEY, CONTAINER, RESULT ) \
		assert( ( RESULT ) == ( std::find_if( ( CONTAINER ).begin(), ( CONTAINER ).end(), \
			[ key = &( *( KEY ) ) ]( const Key& k ) { return key == & k; } ) != ( CONTAINER ).end() ) );

	Action::enact(
		this,
		// Do
		[ this, key, clashingKey, time ] {
			// check state is as expected
			key->throwIfStateNotAsExpected( nullptr, false, time );
			if( clashingKey )
				clashingKey->throwIfStateNotAsExpected( this, true, time );
			// NOTE : If there is a clashing key,
			//          replace clashing key with key in active container
			//          insert clashing key into inactive keys container
			//        else insert key into active keys container
			// NOTE : It is critical to the following code that the key comparison NEVER throws.
			assert( ! key->m_hook.is_linked() );
			if( clashingKey )
			{
				assert( clashingKey->m_hook.is_linked() );
				ASSERTCONTAINSKEY( key, m_keys, false )
				ASSERTCONTAINSKEY( key, m_inactiveKeys, false )
				ASSERTCONTAINSKEY( clashingKey, m_keys, true )
				m_keys.replace_node( m_keys.iterator_to( *clashingKey ), *key );
				ASSERTCONTAINSKEY( key, m_keys, true )
				ASSERTCONTAINSKEY( clashingKey, m_keys, false )
				m_inactiveKeys.insert_before( m_inactiveKeys.lower_bound( time ), *clashingKey );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, true )
				clashingKey->m_active = false;
			}
			else
			{
				assert( m_keys.count( key->m_time ) == static_cast< Keys::size_type >( 0 ) );
				ASSERTCONTAINSKEY( key, m_keys, false )
				m_keys.insert( *key );
				ASSERTCONTAINSKEY( key, m_keys, true )
			}
			key->m_parent = this; // NOTE : never throws or fails
			key->addRef();        // NOTE : take ownership
			key->m_active = true;

			// update keys
			//
			// NOTE : only update the new next/prev keys when there is no active clashing key as
			//        the key being added will replace any inactive clashing key. Always update
			//        the key being added.
			key->m_in.update();
			key->m_out.update();
			if( ! clashingKey )
			{
				if( Key* const kn = key->nextKey() ){ kn->m_in.update(); }
				if( Key* const kp = key->prevKey() ){ kp->m_out.update(); }
			}

			m_keyAddedSignal( this, key.get() );
			propagateDirtiness( outPlug() );
		},
		// Undo
		[ this, key, clashingKey, time ] {
			// check state is as expected
			key->throwIfStateNotAsExpected( this, true, time );
			if( clashingKey )
				clashingKey->throwIfStateNotAsExpected( this, false, time );
			// NOTE : If there was a clashing key
			//          remove the clashing key from inactive keys container
			//          replace key with clashing key in active keys container
			//        else
			//          remove key from active keys container
			// NOTE : It is critical to the following code that the key comparison NEVER throws.
			Key* const kn = key->nextKey();
			Key* const kp = key->prevKey();
			assert( key->m_hook.is_linked() );
			if( clashingKey )
			{
				assert( clashingKey->m_hook.is_linked() );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, true )
				m_inactiveKeys.erase( m_inactiveKeys.iterator_to( *clashingKey ) );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, false )
				ASSERTCONTAINSKEY( key, m_keys, true )
				m_keys.replace_node( m_keys.iterator_to( *key ), *clashingKey );
				Key::Dispose()( key.get() );
				ASSERTCONTAINSKEY( clashingKey, m_keys, true )
				ASSERTCONTAINSKEY( key, m_keys, false )
				clashingKey->m_active = true;
			}
			else
			{
				assert( key->m_active == true );
				ASSERTCONTAINSKEY( key, m_keys, true )
				m_keys.erase_and_dispose( m_keys.iterator_to( *key ), Key::Dispose() );
				ASSERTCONTAINSKEY( key, m_keys, false )
			}

			// update keys
			//
			// NOTE : only update the old next/prev keys when there is no inactive clashing key as
			//        any inactive clashing key will replace the key being removed. Always update
			//        the key being removed and the inactive clashing key as it becomes active.
			key->m_in.update();
			key->m_out.update();
			if( clashingKey )
			{
				clashingKey->m_in.update();
				clashingKey->m_out.update();
			}
			else
			{
				if( kn ){ kn->m_in.update(); }
				if( kp ){ kp->m_out.update(); }
			}

			m_keyRemovedSignal( this, key.get() );
			propagateDirtiness( outPlug() );
		}
	);

#	undef ASSERTCONTAINSKEY

	// remove the clashing key

	if( clashingKey && removeActiveClashing )
	{
		removeKey( clashingKey );
	}

	return clashingKey;
}

Animation::KeyPtr Animation::CurvePlug::insertKey( const float time )
{
	return insertKeyInternal( time, nullptr );
}

Animation::KeyPtr Animation::CurvePlug::insertKey( const float time, const float value )
{
	return insertKeyInternal( time, & value );
}

Animation::KeyPtr Animation::CurvePlug::insertKeyInternal( const float time, const float* const value )
{
	// find span keys for time

	KeyPtr key;
	Key* lo = nullptr;
	Key* hi = nullptr;

	Keys::iterator hiIt = m_keys.lower_bound( time );
	if( hiIt != m_keys.end() )
	{
		hi = &( *( hiIt ) );

		if( hi->m_time == time )
		{
			key = hi;
		}
		else if( hiIt != m_keys.begin() )
		{
			lo = &( *( --hiIt ) );

			if( lo->m_time == time )
			{
				key = lo;
			}
		}
	}
	else
	{
		lo = finalKey();
	}

	// if key already exists at time then return it with updated value, otherwise if time is
	// outside existing range of keys, there is no way currently to extrapolate a value so
	// if no value has been provided then return KeyPtr().

	if( key )
	{
		if( value != nullptr )
		{
			key->setValue( *value );
		}

		return key;
	}
	else if( !( lo && hi ) && ( value == nullptr ) )
	{
		return key;
	}

	// get interpolator

	ConstInterpolatorPtr interpolator = Interpolator::getDefault();

	if( lo )
	{
		interpolator = lo->m_interpolator;
	}
	else if( const Key* const kf = firstKey() )
	{
		interpolator = kf->m_interpolator;
	}

	assert( interpolator );

	// create key

	key.reset( new Key( time, ( ( value != nullptr ) ? ( *value ) : 0.f ), interpolator->getInterpolation() ) );

	// if specified value is the same as the evaluated value of the curve then bisect span.

	if( ( lo && hi ) && ( ( value == nullptr ) || ( evaluate( time ) == ( *value ) ) ) )
	{
		// NOTE : bisection code will replace this.

		key->m_value = evaluate( time );

		// add new key to curve

		addKey( key );
	}
	else
	{
		// only compute auto slope when we have a valid span

		if( lo && hi )
		{
			// NOTE : auto slope code will go here
		}

		// add new key to curve

		addKey( key );
	}

	return key;
}

bool Animation::CurvePlug::hasKey( const float time ) const
{
	return m_keys.find( time ) != m_keys.end();
}

Animation::Key *Animation::CurvePlug::getKey( const float time )
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->getKey( time ) );
}

const Animation::Key *Animation::CurvePlug::getKey( const float time ) const
{
	Keys::const_iterator it = m_keys.find( time );
	return ( it != m_keys.end() )
		? &( *it )
		: nullptr;
}

void Animation::CurvePlug::removeKey( const Animation::KeyPtr &key )
{
	if( key->m_parent != this )
	{
		throw IECore::Exception( "Key is not a child" );
	}

	// save the time of the key at the point it is removed in case it is subsequently
	// added back to the curve and changes are made whilst the key is outside
	// the curve (these changes will not be recorded in the undo/redo system)
	// when undo is called we can then check for any change and throw an exception
	// if time is not as we expect it to be. principle here is that the user should
	// not make changes outside the undo system so if they have then let them know.

	const float time = key->m_time;

	// if key is active find first clashing inactive key
	KeyPtr clashingKey;
	if( key->m_active )
	{
		const InactiveKeys::iterator it = m_inactiveKeys.find( key->m_time );
		if( it != m_inactiveKeys.end() )
		{
			clashingKey = &( *it );
		}
	}

	const bool active = key->m_active;

#	define ASSERTCONTAINSKEY( KEY, CONTAINER, RESULT ) \
		assert( ( RESULT ) == ( std::find_if( ( CONTAINER ).begin(), ( CONTAINER ).end(), \
			[ key = &( *( KEY ) ) ]( const Key& k ) { return key == & k; } ) != ( CONTAINER ).end() ) );

	Action::enact(
		this,
		// Do
		[ this, key, clashingKey, active, time ] {
			// check state is as expected
			key->throwIfStateNotAsExpected( this, active, time );
			if( clashingKey )
				clashingKey->throwIfStateNotAsExpected( this, false, time );
			// NOTE : If key is inactive,
			//          remove key from inactive keys container
			//        else if there is a clashing key
			//          remove the clashing key from inactive keys container
			//          replace key with clashing key in active keys container
			//        else
			//          remove key from active keys container
			// NOTE : It is critical to the following code that the key comparison NEVER throws.
			Key* const kn = key->nextKey();
			Key* const kp = key->prevKey();
			assert( key->m_hook.is_linked() );
			if( ! active )
			{
				ASSERTCONTAINSKEY( key, m_inactiveKeys, true )
				m_inactiveKeys.erase_and_dispose( m_inactiveKeys.iterator_to( *key ), Key::Dispose() );
				ASSERTCONTAINSKEY( key, m_inactiveKeys, false )
			}
			else if( clashingKey )
			{
				assert( clashingKey->m_hook.is_linked() );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, true )
				m_inactiveKeys.erase( m_inactiveKeys.iterator_to( *clashingKey ) );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, false )
				ASSERTCONTAINSKEY( key, m_keys, true )
				m_keys.replace_node( m_keys.iterator_to( *key ), *clashingKey );
				Key::Dispose()( key.get() );
				ASSERTCONTAINSKEY( key, m_keys, false )
				ASSERTCONTAINSKEY( clashingKey, m_keys, true )
				clashingKey->m_active = true;
			}
			else
			{
				ASSERTCONTAINSKEY( key, m_keys, true )
				m_keys.erase_and_dispose( m_keys.iterator_to( *key ), Key::Dispose() );
				ASSERTCONTAINSKEY( key, m_keys, false )
			}

			// update keys
			//
			// NOTE : only update the old next/prev keys when there is no inactive clashing key as
			//        any inactive clashing key will replace the key being removed. Always update
			//        the key being removed and the inactive clashing key as it becomes active.
			key->m_in.update();
			key->m_out.update();
			if( clashingKey )
			{
				clashingKey->m_in.update();
				clashingKey->m_out.update();
			}
			else
			{
				if( kn ){ kn->m_in.update(); }
				if( kp ){ kp->m_out.update(); }
			}

			m_keyRemovedSignal( this, key.get() );
			propagateDirtiness( outPlug() );
		},
		// Undo
		[ this, key, clashingKey, active, time ] {
			// check state is as expected
			key->throwIfStateNotAsExpected( nullptr, false, time );
			if( clashingKey )
				clashingKey->throwIfStateNotAsExpected( this, true, time );
			// NOTE : If key was inactive reinsert key into inactive container
			//        else if there was a clashing key
			//          replace clashing key with key in active container
			//          reinsert clashing key into inactive keys container
			//        else reinsert key into active keys container
			// NOTE : It is critical to the following code that the key comparison NEVER throws.
			assert( ! key->m_hook.is_linked() );
			if( ! active )
			{
				ASSERTCONTAINSKEY( key, m_keys, false )
				ASSERTCONTAINSKEY( key, m_inactiveKeys, false )
				m_inactiveKeys.insert_before( m_inactiveKeys.lower_bound( time ), *key );
				ASSERTCONTAINSKEY( key, m_inactiveKeys, true )
			}
			else if( clashingKey )
			{
				assert( clashingKey->m_hook.is_linked() );
				ASSERTCONTAINSKEY( key, m_keys, false )
				ASSERTCONTAINSKEY( key, m_inactiveKeys, false )
				ASSERTCONTAINSKEY( clashingKey, m_keys, true )
				m_keys.replace_node( m_keys.iterator_to( *clashingKey ), *key );
				ASSERTCONTAINSKEY( key, m_keys, true )
				ASSERTCONTAINSKEY( clashingKey, m_keys, false )
				m_inactiveKeys.insert_before( m_inactiveKeys.lower_bound( time ), *clashingKey );
				ASSERTCONTAINSKEY( clashingKey, m_inactiveKeys, true )
				clashingKey->m_active = false;
			}
			else
			{
				assert( m_keys.count( key->m_time ) == static_cast< Keys::size_type >( 0 ) );
				ASSERTCONTAINSKEY( key, m_keys, false )
				m_keys.insert( *key );
				ASSERTCONTAINSKEY( key, m_keys, true )
			}
			key->m_parent = this; // NOTE : never throws or fails
			key->addRef();        // NOTE : take ownership
			key->m_active = active;

			// update keys
			//
			// NOTE : only update the new next/prev keys when there is no active clashing key as
			//        the key being added will replace any active clashing key. only update the key
			//        being added when it becomes active.
			if( active )
			{
				key->m_in.update();
				key->m_out.update();
			}
			if( ! clashingKey )
			{
				if( Key* const k = key->nextKey() ){ k->m_in.update(); }
				if( Key* const k = key->prevKey() ){ k->m_out.update(); }
			}

			m_keyAddedSignal( this, key.get() );
			propagateDirtiness( outPlug() );
		}
	);

#	undef ASSERTCONTAINSKEY
}

void Animation::CurvePlug::removeInactiveKeys()
{
	for( InactiveKeys::iterator it = m_inactiveKeys.begin(), itEnd = m_inactiveKeys.end(); it != itEnd; )
	{
		removeKey( &( *it++ ) );
	}
}

Animation::Key *Animation::CurvePlug::closestKey( const float time )
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->closestKey( time ) );
}

const Animation::Key *Animation::CurvePlug::closestKey( const float time ) const
{
	if( m_keys.empty() )
	{
		return nullptr;
	}

	Keys::const_iterator rightIt = m_keys.lower_bound( time );
	if( rightIt == m_keys.end() )
	{
		return &( *( m_keys.rbegin() ) );
	}
	else if( rightIt->m_time == time || rightIt == m_keys.begin() )
	{
		return &( *( rightIt ) );
	}
	else
	{
		Keys::const_iterator leftIt = std::prev( rightIt );
		return &( *( std::fabs( time - leftIt->m_time ) < std::fabs( time - rightIt->m_time ) ? leftIt : rightIt ) );
	}
}

Animation::Key *Animation::CurvePlug::closestKey( const float time, const float maxDistance )
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->closestKey( time, maxDistance ) );
}

const Animation::Key *Animation::CurvePlug::closestKey( const float time, const float maxDistance ) const
{
	const Key* const candidate = closestKey( time );

	if( !candidate || ( std::fabs( candidate->m_time - time ) > maxDistance ) )
	{
		return nullptr;
	}

	return candidate;
}

Animation::Key *Animation::CurvePlug::previousKey( const float time )
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->previousKey( time ) );
}

const Animation::Key *Animation::CurvePlug::previousKey( const float time ) const
{
	Keys::const_iterator rightIt = m_keys.lower_bound( time );
	return ( rightIt != m_keys.begin() )
		? &( *( std::prev( rightIt ) ) )
		: nullptr;
}

Animation::Key *Animation::CurvePlug::nextKey( const float time )
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->nextKey( time ) );
}

const Animation::Key *Animation::CurvePlug::nextKey( const float time ) const
{
	Keys::const_iterator rightIt = m_keys.upper_bound( time );
	return ( rightIt != m_keys.end() )
		? &( *( rightIt ) )
		: nullptr;
}

Animation::Key *Animation::CurvePlug::firstKey()
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->firstKey() );
}

Animation::Key *Animation::CurvePlug::finalKey()
{
	return const_cast< Key* >( static_cast< const CurvePlug* >( this )->finalKey() );
}

const Animation::Key *Animation::CurvePlug::firstKey() const
{
	const Key* k = 0;

	if( ! m_keys.empty() )
	{
		k = &( *( m_keys.cbegin() ) );
	}

	return k;
}

const Animation::Key *Animation::CurvePlug::finalKey() const
{
	const Key* k = 0;

	if( ! m_keys.empty() )
	{
		k = &( *( m_keys.crbegin() ) );
	}

	return k;
}

Animation::CurvePlug::TimeKey::type Animation::CurvePlug::TimeKey::operator()( const Animation::Key& key ) const
{
	return key.getTime();
}

Animation::KeyIterator Animation::CurvePlug::begin()
{
	return m_keys.begin();
}

Animation::KeyIterator Animation::CurvePlug::end()
{
	return m_keys.end();
}

Animation::ConstKeyIterator Animation::CurvePlug::begin() const
{
	return m_keys.begin();
}

Animation::ConstKeyIterator Animation::CurvePlug::end() const
{
	return m_keys.end();
}

float Animation::CurvePlug::evaluate( const float time ) const
{
	// NOTE : no keys return 0

	if( m_keys.empty() )
	{
		return 0.f;
	}

	// NOTE : each key determines value at a specific time therefore only
	//        interpolate for times which are between the keys.

	Keys::const_iterator hiIt = m_keys.lower_bound( time );
	if( hiIt == m_keys.end() )
	{
		return ( m_keys.rbegin() )->getValue();
	}

	const Key &hi = *( hiIt );

	if( hi.m_time == time || hiIt == m_keys.begin() )
	{
		return hi.getValue();
	}

	const Key &lo = *( std::prev( hiIt ) );

	// normalise time to lo, hi key time range

	const double dt = lo.m_out.m_dt;
	const double nt = Imath::clamp( ( time - lo.m_time ) / dt, 0.0, 1.0 );

	// evaluate interpolator

	return lo.m_interpolator->evaluate( lo, hi, nt, dt );
}

FloatPlug *Animation::CurvePlug::outPlug()
{
	return getChild<FloatPlug>( 0 );
}

const FloatPlug *Animation::CurvePlug::outPlug() const
{
	return getChild<FloatPlug>( 0 );
}

//////////////////////////////////////////////////////////////////////////
// Animation implementation
//////////////////////////////////////////////////////////////////////////

GAFFER_NODE_DEFINE_TYPE( Animation );

size_t Animation::g_firstPlugIndex = 0;

Animation::Animation( const std::string &name )
	:	ComputeNode( name )
{
	storeIndexOfNextChild( g_firstPlugIndex );

	addChild( new Plug( "curves" ) );
}

Animation::~Animation()
{
}

Plug *Animation::curvesPlug()
{
	return getChild<Plug>( g_firstPlugIndex );
}

const Plug *Animation::curvesPlug() const
{
	return getChild<Plug>( g_firstPlugIndex );
}

bool Animation::canAnimate( const ValuePlug* const plug )
{
	if( !plug->getFlags( Plug::AcceptsInputs ) )
	{
		return false;
	}

	if( plug->getInput() && !isAnimated( plug ) )
	{
		return false;
	}

	const Node *node = plug->node();
	if( !node || !node->parent<Node>() )
	{
		// Nowhere to parent our Animation node.
		return false;
	}

	return
		IECore::runTimeCast<const FloatPlug>( plug ) ||
		IECore::runTimeCast<const IntPlug>( plug ) ||
		IECore::runTimeCast<const BoolPlug>( plug );
}

bool Animation::isAnimated( const ValuePlug* const plug )
{
	return inputCurve( plug );
}

Animation::CurvePlug *Animation::acquire( ValuePlug* const plug )
{
	// If the plug is already driven by a curve, return it.
	if( CurvePlug *curve = inputCurve( plug ) )
	{
		return curve;
	}

	// Otherwise we need to make one. Try to find an
	// existing Animation driving plugs on the same node.

	AnimationPtr animation;
	if( !plug->node() )
	{
		throw IECore::Exception( "Plug does not belong to a node" );
	}

	for( Plug::RecursiveIterator it( plug->node() ); !it.done(); ++it )
	{
		ValuePlug *valuePlug = IECore::runTimeCast<ValuePlug>( it->get() );
		if( !valuePlug )
		{
			continue;
		}

		if( CurvePlug *curve = inputCurve( valuePlug ) )
		{
			animation = IECore::runTimeCast<Animation>( curve->node() );
			if( animation )
			{
				break;
			}
		}
	}

	// If we couldn't find an existing Animation, then
	// make one.
	if( !animation )
	{
		Node *parent = plug->node()->parent<Node>();
		if( !parent )
		{
			throw IECore::Exception( "Node does not have a parent" );
		}
		animation = new Animation;
		parent->addChild( animation );
	}

	// Add a curve to the animation, and hook it up to
	// the target plug.

	CurvePlugPtr curve = new CurvePlug( "curve0", Plug::In, Plug::Default | Plug::Dynamic );
	animation->curvesPlug()->addChild( curve );

	plug->setInput( curve->outPlug() );

	return curve.get();
}

Animation::CurvePlug *Animation::inputCurve( ValuePlug* const plug )
{
	ValuePlug *source = plug->source<ValuePlug>();
	if( source == plug ) // no input
	{
		return nullptr;
	}

	CurvePlug *curve = source->parent<CurvePlug>();
	if( !curve )
	{
		return nullptr;
	}

	if( source == curve->outPlug() )
	{
		return curve;
	}

	return nullptr;
}

const Animation::CurvePlug *Animation::inputCurve( const ValuePlug* const plug )
{
	// preferring cast over maintaining two near-identical methods.
	return inputCurve( const_cast<ValuePlug *>( plug ) );
}

void Animation::affects( const Plug* const input, AffectedPlugsContainer &outputs ) const
{
	ComputeNode::affects( input, outputs );
}

void Animation::hash( const ValuePlug* const output, const Context* const context, IECore::MurmurHash &h ) const
{
	ComputeNode::hash( output, context, h );

	if( const CurvePlug *parent = output->parent<CurvePlug>() )
	{
		h.append( parent->evaluate( context->getTime() ) );
	}
}

void Animation::compute( ValuePlug* const output, const Context* const context ) const
{
	if( const CurvePlug *parent = output->parent<CurvePlug>() )
	{
		static_cast<FloatPlug *>( output )->setValue( parent->evaluate( context->getTime() ) );
		return;
	}

	ComputeNode::compute( output, context );
}

ValuePlug::CachePolicy Animation::computeCachePolicy( const Gaffer::ValuePlug* const output ) const
{
	if( output->parent<CurvePlug>() )
	{
		return ValuePlug::CachePolicy::Uncached;
	}

	return ComputeNode::computeCachePolicy( output );
}

Animation::Interpolation Animation::defaultInterpolation()
{
	return Interpolator::getDefault()->getInterpolation();
}

Animation::Direction Animation::opposite( const Animation::Direction direction )
{
	return static_cast< Direction >( ( static_cast< int >( direction ) + 1 ) % 2 );
}

double Animation::defaultSlope()
{
	return 0.0;
}

double Animation::defaultScale()
{
	return ( 1.0 / 3.0 );
}

const char* Animation::toString( const Animation::Interpolation interpolation )
{
	switch( interpolation )
	{
		case Interpolation::Constant:
			return "Constant";
		case Interpolation::ConstantNext:
			return "ConstantNext";
		case Interpolation::Linear:
			return "Linear";
		case Interpolation::Cubic:
			return "Cubic";
		case Interpolation::Bezier:
			return "Bezier";
		default:
			assert( 0 );
			return 0;
	}
}

const char* Animation::toString( const Animation::Direction direction )
{
	switch( direction )
	{
		case Direction::In:
			return "In";
		case Direction::Out:
			return "Out";
		default:
			assert( 0 );
			return 0;
	}
}

} // Gaffer
