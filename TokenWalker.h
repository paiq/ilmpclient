/*
 * ILMP client library - http://opensource.implicit-link.com/
 * Copyright (c) 2010 Implicit Link
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ILMPCLIENT_TOKEN_WALKER_H
#define ILMPCLIENT_TOKEN_WALKER_H

#include <boost/tokenizer.hpp>

struct TokenExpectedException { };

// A TokenWalker iterates over some source that emits chars, then uses
// boost::tokenizer to convert this into tokens based on a separator char. 
template <class SI> // SI: Source iterator
class TokenWalker {
	typedef boost::char_separator<char> separator;
public:
	TokenWalker(const SI& beginIterator, const SI& endIterator, char _sep, bool emptyTokens = false) :
			tokenizer(beginIterator, endIterator, separator(std::string(&_sep, 1).c_str(), "", emptyTokens ? boost::keep_empty_tokens : boost::drop_empty_tokens)),
			iterator(tokenizer.begin()) {}

	bool tryNext(int& i, int def = 0) {
		std::string s;
		if (tryNext(s)) {
			i = atoi(s.c_str());
			return true;
	 	}
		i = def;
		return false;
	}
	
	bool tryNext(std::string& s, const std::string& def = "") {
		if (iterator != tokenizer.end()) {
			s = *(iterator++);
			return true;
		}
		s = def;
		return false;
	}

	template<class T>
	void next(T& i) {
		if (!tryNext(i))
			throw TokenExpectedException(); 
	}
	
	bool skip() {
		std::string vd;
		return tryNext(vd);
	}

private:
	boost::tokenizer<separator, SI> tokenizer;
	class boost::tokenizer<separator, SI>::iterator iterator; 
};

// Implementation for walking a boost::asio::streambuf
class StreamTokenWalker : public TokenWalker<std::istreambuf_iterator<char> > {
public:
	StreamTokenWalker(boost::asio::streambuf& s, char sep, bool emptyTokens = false) :
		TokenWalker<std::istreambuf_iterator<char> >(std::istreambuf_iterator<char>(&s), std::istreambuf_iterator<char>(), sep, emptyTokens) {}
};

// Implementation for walking a std::string
class StringTokenWalker : public TokenWalker<std::string::const_iterator> {
public:
	StringTokenWalker(const std::string& s, char sep, bool emptyTokens = false) :
		TokenWalker<std::string::const_iterator>(s.begin(), s.end(), sep, emptyTokens) {}
};

#endif
