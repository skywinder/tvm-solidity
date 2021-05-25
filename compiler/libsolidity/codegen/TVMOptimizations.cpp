/*
 * Copyright 2018-2020 TON DEV SOLUTIONS LTD.
 *
 * Licensed under the  terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the  GNU General Public License for more details at: https://www.gnu.org/licenses/gpl-3.0.html
 */
/**
 * @author TON Labs <connect@tonlabs.io>
 * @date 2020
 * TVM codegen driver
 */

#include "TVMOptimizations.hpp"
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>

namespace solidity::frontend {

struct TVMOptimizer {
	vector<string>	lines_;

	static bool is_space(char ch) {
		return ch == ' ' || ch == '\t';
	}

	static bool is_comment_or_empty_line(const string& str) {
		for (auto ch : str) {
			if (is_space(ch))
				continue;
			return ch == ';';
		}
		// empty line
		return true;
	}

	static int strToInt(const std::string& str) {
		const std::string& trimed = boost::algorithm::trim_copy(str);
		return boost::lexical_cast<int>(trimed);
	}

	struct Cmd {
		string prefix_, cmd_, rest_, comment_;

		bool is_simple_command_{false};
		int inputs_count_{0}, outputs_count_{0};

		explicit Cmd(const string& str) {
			int i = 0, n = str.size();
			while (i < n && is_space(str[i]))
				prefix_.push_back(str[i++]);
			while (i < n && !is_space(str[i]) && str[i] != ';')
				cmd_.push_back(str[i++]);
			while (i < n && is_space(str[i]))
				i++;
			while (i < n && str[i] != ';')
				rest_.push_back(str[i++]);
			analyze();
		}

		bool is(const string& cmd) const {
			return cmd_ == cmd;
		}

		string rest() const {
			return rest_;
		}

		string without_prefix() const {
			if (rest_.empty()) return cmd_;
			return cmd_ + " " + rest_;
		}

		bigint fetch_bigint() const {
			std::string trimed = boost::algorithm::trim_copy(rest_);
			return bigint{trimed};
		}

		int fetch_int() const {
			return strToInt(rest_);
		}

		int fetch_first_int() const {
			size_t i = rest_.find(',');
			solAssert(i != string::npos, "");
			return strToInt(rest_.substr(0, i));
		}

		int fetch_second_int() const {
			size_t i = rest_.find(',');
			solAssert(i != string::npos, "");
			i++;
			while (i < rest_.size() && is_space(rest_[i]))
				i++;
			solAssert(i != rest_.size(), "");
			return strToInt(rest_.substr(i));
		}

		bool is_drop_kind() const {
			return get_drop_index() > 0;
		}

		int get_drop_index() const {
			if (is_DROP())
				return 1;
			if (is("DROP2"))
				return 2;
			if (is("BLKDROP"))
				return fetch_int();
			return 0;
		}

		int sumBLKSWAP() const {
			if (is("ROT") || is("ROTREV")) {
				return 3;
			}
			if (is("SWAP2")) {
				return 4;
			}
			if (is("BLKSWAP")) {
				return fetch_first_int() + fetch_second_int();
			}
			solUnimplemented("");
		}

		int fetchStackIndex() const {
			if (cmd_ == "DUP") {
				return 0;
			}
			string s = rest();
			solAssert(isIn(s.at(0), 's', 'S'), "");
			s.erase(s.begin()); // skipping char S
			return strToInt(s);
		}

		int get_push_index() const {
			solAssert(is_PUSH(), "");
			if (is_DUP()) return 0;
			return fetchStackIndex();
		}

		std::pair<int, int> get_push2_indexes() const {
			solAssert(is("PUSH2"), "");
			std::string target = rest();
			std::smatch sm;
			std::regex re1(R"([S|s](\d+),\s*[S|s](\d+))");
			std::regex_search(target, sm, re1);

			int si = strToInt(sm[1]);
			int sj = strToInt(sm[2]);
			return {si, sj};
		}

		int get_pop_index() const {
			solAssert(is_POP(), "");
			string s = rest();
			s.erase(s.begin());
			return strToInt(s);
		}

		bool is_commutative() const {
			return is_ADD() || is_MUL() || is("AND") || is("OR") || is("XOR") ||
					is("EQUAL") || is("NEQ");
		}

		bool is_add_or_sub() const {
			return is_ADD() || is_SUB();
		}

		bool is_ADD() const 	{ 	return is("ADD"); 		}
		bool is_MUL() const 	{ 	return is("MUL");		}
		bool is_DIV() const 	{ 	return is("DIV");		}
		bool is_SUB() const 	{ 	return is("SUB"); 		}
		bool is_DROP() const 	{ 	return is("DROP"); 		}
		bool is_NIP() const 	{ 	return is("NIP"); 		}
		bool is_SWAP() const 	{ 	return is("SWAP"); 		}
		bool is_DUP() const 	{ 	return is("DUP"); 		}
		bool is_PUSH() const 	{ 	return (is("PUSH") && ((boost::starts_with(rest_, "S") || boost::starts_with(rest_, "s")))) || is("DUP"); 		}

		bool is_PUSHINT() const {
			int i{};
			for (char ch : boost::algorithm::trim_copy(rest_)) {
				if (!(isdigit(ch) || (i == 0 && ch=='-'))) {
					return false; // e.g. PUSHINT $func_name$
				}
				++i;
			}
			return is("PUSHINT");
		}

		bool is_POP() const 	{ 	return is("POP"); 		}
		bool isBLKSWAP() const  { return is("ROT") || is("ROTREV") || is("SWAP2") || is("BLKSWAP"); }

		bool is_const_add() const { return is("INC") || is("DEC") || is("ADDCONST"); }

		int get_add_num() const {
			solAssert(is_const_add(), "");
			if (is("INC"))
				return 1;
			if (is("DEC"))
				return -1;
			if (is("ADDCONST"))
				return fetch_int();
			solUnimplemented("");
		}

		bool is_simple_command(int inp, int outp) const {
			return is_simple_command_ &&
				   inp == inputs_count_ &&
				   outp == outputs_count_;
		}

	private:
		void set_simple_command(int inp, int outp) {
			is_simple_command_ = true;
			inputs_count_  = inp;
			outputs_count_ = outp;
		}

		bool try_simple_command(const set<string>& s, int inp, int outp) {
			if (s.count(cmd_)) {
				set_simple_command(inp, outp);
				return true;
			}
			return false;
		}

		void analyze() {
			static const set<string> s01 {
				"GETGLOB",
				"NEWC",
				"NEWDICT",
				"NOW",
				"PUSHINT",
				"PUSHSLICE",
				"TRUE",
				"FALSE",
				"ZERO",
			};
			static const set<string> s10 {
				"DROP",
				"ENDS",
				"SETGLOB",
				"THROWANY",
				"THROWIF",
				"THROWIFNOT",
			};
			static const set<string> s11 {
				"CTOS",
				"DEC",
				"ENDC",
				"EQINT",
				"FIRST",
				"FITS",
				"HASHCU",
				"HASHSU",
                "VERGRTH16",
				"INC",
				"INDEX",
				"NOT",
				"PARSEMSGADDR",
				"SBITS",
				"SECOND",
				"SHA256U",
				"STSLICECONST",
				"THIRD",
				"UFITS",
			};
			static const set<string> s21 {
				"ADD",
				"AND",
				"EQ",
				"GREATER",
				"INDEXVAR",
				"LESS",
				"MUL",
				"NEQ",
				"OR",
				"PAIR",
				"PLDUX",
				"SETINDEX",
				"STI",
				"STSLICE",
				"STU",
				"SUB",
		   		"DIV",
		   		"MOD",
		   		"SUBR",
			};
			static const set<string> s32 {
				"DICTDEL",
				"DICTIDEL",
				"DICTUDEL",
			};

			if (try_simple_command(s01, 0, 1)) return;
			if (try_simple_command(s10, 1, 0)) return;
			if (try_simple_command(s11, 1, 1)) return;
			if (try_simple_command(s21, 2, 1)) return;
			if (try_simple_command(s32, 3, 2)) return;

			if (is("SWAP"))
				return set_simple_command(2, 2);
			if (is("ROT") || is("ROTREV"))
				return set_simple_command(3, 3);
			if (is("TUPLE"))
				return set_simple_command(fetch_int(), 1);
			if (is("UNTUPLE"))
				return set_simple_command(1, fetch_int());
			if (is("UNPAIR"))
				return set_simple_command(1, 2);
			if (is("SETINDEXVAR"))
				return set_simple_command(3, 1);
		}

	};

	static string get_cmd(const string& str) {
		return Cmd(str).cmd_;
	}

	Cmd cmd(int idx) const {
		if (valid(idx))
			return Cmd(lines_[idx]);
		return Cmd("");
	}

	int next_command_line(int idx) const {
		if (!valid(idx)) return -1;
		idx++;
		while (true) {
			if (!valid(idx))
				return -1;
			if (!is_comment_or_empty_line(lines_[idx]))
				return idx;
			idx++;
		}
	}

	bool valid(int idx) const {
		return idx >= 0 && size_t(idx) < lines_.size();
	}

	void remove(int idx) {
		lines_.erase(lines_.begin() + idx);
	}

	void insert(int idx, const string& cmd, const string& pfx = "") {
		lines_.insert(lines_.begin() + idx, pfx + cmd);
	}

//	bool is_cmd(int idx, const string& cmd) const {
//		return valid(idx) && get_cmd(lines_[idx]) == cmd;
//	}

	struct Result {
		bool continue_;
		int remove_ = 0;
		vector<string> commands_;

		explicit Result(bool cont, int remove = 0, vector<string> commands = {}) :
			continue_(cont), remove_(remove), commands_{std::move(commands)} {

		}

		template <class ...Args>
		static Result Replace(int remove, Args... cmds) {
			return Result(true, remove, {cmds...});
		}

		static Result Comment(const string& cmd) {
			Result res(false);
			res.commands_.push_back(cmd);
			return res;
		}
	};

	Result optimize_at(const int idx1) const {
		int idx2 = next_command_line(idx1);
		int idx3 = next_command_line(idx2);
		int idx4 = next_command_line(idx3);
		int idx5 = next_command_line(idx4);
		int idx6 = next_command_line(idx5);
		Cmd cmd1 = cmd(idx1);
		Cmd cmd2 = cmd(idx2);
		Cmd cmd3 = cmd(idx3);
		Cmd cmd4 = cmd(idx4);
		Cmd cmd5 = cmd(idx5);
		Cmd cmd6 = cmd(idx6);
		// TODO: INC + UFITS256...
		if (cmd1.is_SWAP()) {
			if (cmd2.is_SUB())		return Result::Replace(2, "SUBR");
			if (cmd2.is("SUBR"))	return Result::Replace(2, "SUB");
			if (cmd2.is_SWAP())		return Result::Replace(2);
			if (cmd2.is_NIP())		return Result::Replace(2, "DROP");
			if (cmd2.is_commutative())	return Result::Replace(1);
		}
		if (cmd1.is_PUSHINT() && cmd3.is_PUSHINT()) {
			// TODO: consider INC/DEC as well
			if (cmd2.is_add_or_sub() && cmd4.is_add_or_sub()) {
				bigint sum = 0;
				sum += (cmd2.is_ADD()? +1 : -1) * cmd1.fetch_bigint();
				sum += (cmd4.is_ADD()? +1 : -1) * cmd3.fetch_bigint();
				return Result::Replace(4, "PUSHINT " + toString(sum), "ADD");
			}
		}
		if (cmd1.is_PUSHINT()) {
			if (cmd1.rest() == "1") {
				if (cmd2.is_ADD()) return Result::Replace(2, "INC");
				if (cmd2.is_SUB()) return Result::Replace(2, "DEC");
			}
			bigint value = cmd1.fetch_bigint();
			if (-128 <= value && value <= 127) {
				if (cmd2.is_ADD()) return Result::Replace(2, "ADDCONST " + toString(value));
				if (cmd2.is_MUL()) return Result::Replace(2, "MULCONST " + toString(value));
			}
			if (-128 <= -value && -value <= 127) {
				if (cmd2.is_SUB()) return Result::Replace(2, "ADDCONST " + toString(-value));
			}
		}
		if (cmd1.is("RET") || cmd1.is("THROWANY") || cmd1.is("THROW")) {
			// delete commands after noreturn opcode
			if (cmd2.prefix_.length() >= cmd1.prefix_.length() &&  !cmd2.cmd_.empty())
				return Result::Replace(2, cmd1.without_prefix());
		}
		if (cmd1.is("RET") && cmd2.is("}")) {
			return Result::Replace(2, "}");
		}
		if (cmd2.is_NIP() && cmd3.is_NIP()) {
			// if (cmd1.is_PUSH() && cmd1.get_push_index() == 1) {
				// return Result::Replace(3, "DROP");
			// }
			if (cmd1.is_PUSHINT() || cmd1.is("GETGLOB")) {
				return Result::Replace(3, "DROP2", cmd1.without_prefix());
			}
			// return Result::Comment(";;;;;;;;;;;;;; NIP+NIP");
		}
		if (cmd1.is_NIP() && cmd2.is_NIP() && cmd3.is_NIP()) {
			int i = idx1, n = 0;
			while (cmd(i).is_NIP()) {
				n++;
				i = next_command_line(i);
			}
			if (n > 15) n = 15;
			return Result::Replace(n, "BLKSWAP " + toString(n) + ", 1", "BLKDROP " + toString(n));
		}
		if (cmd1.is_POP() && cmd1.get_pop_index() == 2 && cmd2.is_SWAP()) {
			// TODO: generalize these cases...
			if (cmd3.is_simple_command(1, 0))
				return Result::Replace(3, cmd3.without_prefix(), "NIP");
			if (cmd3.is_simple_command(1, 1) && cmd4.is_simple_command(1, 0))
				return Result::Replace(4, cmd3.without_prefix(), cmd4.without_prefix(), "NIP");
		}
		if (cmd1.is_PUSHINT() && cmd2.is_PUSHINT() && cmd3.is_PUSHINT()) {
			int i = idx1, n = 0;
			while (cmd(i).is_PUSHINT() && cmd(i).fetch_bigint() == 0) {
				n++;
				i = next_command_line(i);
			}
			if (n >= 3) {
				Result res = Result::Replace(n, "PUSHINT 0");
				n--;
				while (n > 0) {
					int nn = std::min(15, n);
					res.commands_.push_back(make_BLKPUSH(nn, 0));
					n -= nn;
				}
				return res;
			}
		}
		if (cmd1.is_PUSH() && cmd1.get_push_index() == 0 && cmd2.is_SWAP()) {
			return Result::Replace(2, cmd1.without_prefix());
		}
		if (cmd3.is_SWAP()) {
			bool ok1 = cmd1.is_simple_command(0, 1) || cmd1.is_PUSH();
			bool ok2 = cmd2.is_simple_command(0, 1) || cmd2.is_PUSH();
			if (ok1 && ok2) {
				if (cmd2.is_PUSH() && cmd2.get_push_index() == 0)
					return Result::Replace(3, cmd1.without_prefix(), cmd2.without_prefix());
				string s1 = cmd2.is_PUSH() ? make_PUSH(cmd2.get_push_index()-1) : cmd2.without_prefix();
				string s2 = cmd1.is_PUSH() ? make_PUSH(cmd1.get_push_index()+1) : cmd1.without_prefix();
				return Result::Replace(3, s1, s2);
			}
		}
		if (cmd1.is_PUSH() && cmd2.is_PUSH()) {
			int i = idx1, n = 0;
			while (cmd(i).is_PUSH() && cmd(i).get_push_index() == cmd1.get_push_index()) {
				n++;
				i = next_command_line(i);
			}
			if (n >= 2 && cmd1.get_push_index() <= 15) {
				if (n > 15) n = 15;
				return Result::Replace(n, make_BLKPUSH(n, cmd1.get_push_index()));
			}
		}
		if ((cmd1.is_PUSH() || cmd1.is_PUSHINT()) && cmd2.is_drop_kind()) {
			if (cmd2.is_DROP()) {
				return Result::Replace(2);
			} else {
				return Result::Replace(2, make_DROP(cmd2.get_drop_index()-1));
			}
		}
		if (cmd1.is("BLKPUSH") && cmd2.is_drop_kind()) {
			int diff = cmd1.fetch_first_int() - cmd2.get_drop_index();
			if (diff == 0)
				return Result::Replace(2);
			if (diff < 0)
				return Result::Replace(2, make_DROP(-diff));
			else
				return Result::Replace(2, make_BLKPUSH(diff, cmd1.fetch_second_int()));
		}
		if (cmd1.is_simple_command_ && cmd1.outputs_count_ == 1 && cmd2.is_drop_kind()) {
			int q = cmd1.inputs_count_ + cmd2.get_drop_index() - 1;
			solAssert(q >= 0, "");
			if (q == 0) return Result::Replace(2);
			return Result::Replace(2, make_DROP(q));
		}
		if (cmd1.is_simple_command_ && cmd1.inputs_count_ == 0 && cmd1.outputs_count_ == 1 && cmd2.is_NIP()) {
			std::vector<std::string> dropOpcodes = make_DROP(1);
			dropOpcodes.push_back(cmd1.without_prefix());
			return Result(true, 2, dropOpcodes);
		}
		if (cmd1.is_NIP() && cmd2.is_drop_kind()) {
			return Result::Replace(2, make_DROP(1 + cmd2.get_drop_index()));
		}
		if (cmd1.isBLKSWAP() && cmd2.is_drop_kind()) {
			int n1 = cmd1.sumBLKSWAP();
			int n2 = cmd2.get_drop_index();
			if (n2 >= n1)
				return Result::Replace(2, cmd2.without_prefix());
		}
		if (cmd1.is_drop_kind() && cmd2.is_drop_kind()) {
			int i = idx1, n = 0, total = 0;
			while (cmd(i).is_drop_kind()) {
				n++;
				total += cmd(i).get_drop_index();
				i = next_command_line(i);
			}
			if (total > 1) {
				return Result::Replace(n, make_DROP(total));
			}
		}
		if (cmd1.is_PUSH() && cmd1.get_push_index() == 0) {
			// Try to remove unneeded DUP..NIP/DROP pair
			vector<string> commands;
			int lines_to_remove = 1;
			if (try_simulate(idx2, 2, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (cmd1.is_PUSH() && cmd1.get_push_index() == 1) {
			// Try to remove unneeded PUSH S1..NIP/DROP pair
			vector<string> commands{"SWAP"};
			int lines_to_remove = 1;
			if (try_simulate(idx2, 3, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (cmd1.is_simple_command(0, 1)) {
			// Try to remove unneeded PUSHINT..NIP/DROP pair
			vector<string> commands;
			int lines_to_remove = 1;
			if (try_simulate(idx2, 1, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (cmd1.is_SWAP()) {
			// Try to remove unneeded SWAP..NIP/DROP pair
			vector<string> commands{"DROP"};
			int lines_to_remove = 1;
			if (try_simulate(idx2, 2, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (!cmd1.is_drop_kind()) {
			// Check if topmost stack element can be dropped
			vector<string> commands{"DROP"};
			int lines_to_remove = 0;
			if (try_simulate(idx1, 1, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (false && !cmd1.is_NIP()) {	// TODO: disabled because this makes things worse
			// Check if second topmost stack element can be dropped
			vector<string> commands{"NIP"};
			int lines_to_remove = 0;
			if (try_simulate(idx1, 2, lines_to_remove, commands))
				return Result{true, lines_to_remove, commands};
		}
		if (cmd1.is("NEWC") && cmd2.is_simple_command(0, 1) &&
			isIn(cmd3.cmd_, "STUR", "STIR", "STBR", "STBREFR", "STSLICER", "STREFR")) {
			return Result::Replace(3,
					cmd2.without_prefix(),
					"NEWC",
					cmd3.cmd_.substr(0, cmd3.cmd_.size() - 1) + " " + cmd3.rest());
		}
		if (cmd1.is("PUSHCONT") && cmd2.is("}") && (cmd3.is("IF") || cmd3.is("IFNOT"))) {
			return Result::Replace(3, "DROP");
		}
		if (cmd1.is("PUSHCONT") && cmd2.is("}") && cmd3.is("IFJMP")) {
			return Result::Replace(3, "IFRET");
		}
		if (cmd1.is("PUSHCONT") && cmd2.is("}") && cmd3.is("IFNOTJMP")) {
			return Result::Replace(3, "IFNOTRET");
		}
		if (cmd1.is("PUSHCONT") &&
			cmd2.is("THROW") &&
			cmd3.is("}") &&
			(cmd4.is("IF") || cmd4.is("IFJMP"))) {
			return Result::Replace(4, "THROWIF " + cmd2.rest());
		}
		if (cmd1.is("PUSHCONT") &&
			cmd2.is("THROW") &&
			cmd3.is("}") &&
			(cmd4.is("IFNOT") || cmd4.is("IFNOTJMP"))) {
			return Result::Replace(4, "THROWIFNOT " + cmd2.rest());
		}
		if (cmd1.is("PUSHCONT") &&
			cmd2.is("}") &&
			(cmd3.is("IF") || cmd3.is("IFNOT"))) {
			return Result::Replace(3, "DROP");
		}
		if (cmd1.is("GETGLOB") &&
			cmd2.is("ISNULL") &&
			cmd3.is("DROP")) {
			return Result::Replace(3, "");
		}
		if ((cmd1.is("NOT") || (cmd1.is("EQINT") && cmd1.fetch_int() == 0)) &&
			cmd2.is("THROWIFNOT")) {
			return Result::Replace(2, "THROWIF " + cmd2.rest());
		}
		if (cmd1.is("NEQINT") && cmd1.fetch_int() == 0 &&
			cmd2.is("THROWIFNOT")) {
			return Result::Replace(2, "THROWIFNOT " + cmd2.rest());
		}
		if (cmd1.is("NOT") &&
			cmd2.is("THROWIF")) {
			return Result::Replace(2, "THROWIFNOT " + cmd2.rest());
		}
		if (cmd1.is_PUSH()) {
			// PUSH Sx
			// XCHG n
			// BLKDROP n
			int pushIndex = cmd1.fetchStackIndex();
			if (cmd2.is("XCHG")) {
				int xghIndex = cmd2.fetchStackIndex();
				if (cmd3.is_drop_kind()) {
					int dropedQty = cmd3.get_drop_index();
					if (xghIndex == dropedQty && dropedQty <= 15) {
						int i = std::min(pushIndex, xghIndex - 1);
						int j = std::max(pushIndex, xghIndex - 1);
						if (i != j) {
							if (pushIndex + 1 < dropedQty) {
								std::vector<std::string> opcodes{"XCHG S" + toString(i) + ", S" + toString(j)};
								std::vector<std::string> dropOpcodes = make_DROP(dropedQty - 1);
								opcodes.insert(opcodes.end(), dropOpcodes.begin(), dropOpcodes.end());
								return Result(true, 3, opcodes);
							}
						} else {
							return Result::Replace(3, make_DROP(dropedQty - 1));
						}
					}
				}
			}
		}
		if (cmd1.is("ROT") && cmd2.is("ROTREV")) {
			return Result::Replace(2);
		}
		if (cmd1.is("ROTREV") && cmd2.is("ROT")) {
			return Result::Replace(2);
		}
		if (cmd1.is_PUSHINT() && cmd2.is("STZEROES") && cmd3.is("STSLICECONST") && cmd3.rest() == "0") {
			return Result::Replace(3, "PUSHINT " + toString(cmd1.fetch_bigint() + 1), "STZEROES");
		}

		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICE") &&
			cmd4.is("STSLICECONST")) {
			std::vector<std::string> opcodes = unitSlices(cmd1.rest(), cmd4.rest());
			if (opcodes.size() == 1) {
				opcodes[0] = "PUSHSLICE " + opcodes[0];
				opcodes.emplace_back("NEWC");
				opcodes.emplace_back("STSLICE");
				return Result(true, 4, opcodes);
			}
		}
		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("STSLICER") &&
			cmd3.is("STSLICECONST")) {
			std::vector<std::string> opcodes = unitSlices(cmd1.rest(), cmd3.rest());
			if (opcodes.size() == 1) {
				opcodes[0] = "PUSHSLICE " + opcodes[0];
				opcodes.emplace_back("STSLICER");
				return Result(true, 3, opcodes);
			}
		}
		if (cmd1.is_PUSHINT() &&
			cmd2.is("STZEROES") &&
			cmd3.is("STSLICECONST") && cmd3.rest().length() > 1) {
			std::string::size_type integer = cmd1.fetch_int();
			std::vector<std::string> opcodes = unitBitString(std::string(integer, '0'), toBitString(cmd3.rest()));
			if (opcodes.size() == 1) {
				opcodes[0] = "PUSHSLICE " + opcodes[0];
				opcodes.emplace_back("STSLICER");
				return Result(true, 3, opcodes);
			}
		}
		if (cmd1.is("STSLICECONST") &&
			cmd2.is("STSLICECONST")) {
			std::vector<std::string> opcodes = unitSlices(cmd1.rest(), cmd2.rest());
			if (opcodes.size() == 1 && toBitString(opcodes[0]).length() <= TvmConst::MaxSTSLICECONST) {
				return Result(true, 2, {"STSLICECONST " + opcodes[0]});
			}
		}
		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICECONST") &&
			cmd4.is("STSLICE")) {
			std::vector<std::string> opcodes = unitSlices(cmd3.rest(), cmd1.rest());
			if (opcodes.size() == 1) {
				return Result(true, 4, {"PUSHSLICE " + opcodes[0], "NEWC", "STSLICE"});
			}
		}
		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICE") &&
			cmd4.is("PUSHSLICE") &&
			cmd5.is("STSLICER")) {
			std::vector<std::string> opcodes = unitSlices(cmd1.rest(), cmd4.rest());
			if (opcodes.size() == 1) {
				return Result(true, 5, {"PUSHSLICE " + opcodes[0], "NEWC", "STSLICE"});
			}
		}
		if (cmd1.is("TUPLE") &&
			cmd2.is("UNTUPLE") &&
			cmd1.fetch_int() == cmd2.fetch_int()) {
			return Result(true, 2, {});
		}
		if (cmd1.is("PAIR") &&
			cmd2.is("UNPAIR")) {
			return Result(true, 2, {});
		}
		if (cmd1.is("ROT") &&
			(cmd2.is("SETGLOB") || (cmd2.is("POP") && cmd2.fetchStackIndex() >= 3)) &&
			cmd3.is("SWAP")) {
			return Result(true, 3, {"XCHG s2", cmd2.without_prefix()});
		}
		if (cmd1.is("SETGLOB") && cmd2.is("GETGLOB") && cmd1.rest() == cmd2.rest()) {
			return Result(true, 2, {"DUP", "SETGLOB " + cmd2.rest()});
		}
		if (cmd1.is_const_add() && cmd2.is_const_add()) {
			int final_add = cmd1.get_add_num() + cmd2.get_add_num();
			if (-128 <= final_add && final_add <= 127)
				return Result(true, 2, {"ADDCONST " + std::to_string(final_add)});
		}
		if (cmd1.is_const_add() && cmd3.is_const_add()) {
			if (cmd2.is("UFITS") && cmd4.is("UFITS") && cmd2.rest() == cmd4.rest()) {
				int final_add = cmd1.get_add_num() + cmd3.get_add_num();
				if (-128 <= final_add && final_add <= 127)
					return Result(true, 4, {"ADDCONST " + std::to_string(final_add), "UFITS " + cmd2.rest()});
			}
		}
		if (cmd1.is("INDEX") && 0 <= strToInt(cmd1.rest()) && strToInt(cmd1.rest()) <= 3 &&
			cmd2.is("INDEX") && 0 <= strToInt(cmd2.rest()) && strToInt(cmd2.rest()) <= 3 &&
			cmd3.is("INDEX") && 0 <= strToInt(cmd3.rest()) && strToInt(cmd3.rest()) <= 3) {
			return Result(true, 3, {"INDEX3 " + cmd1.rest() + ", " + cmd2.rest() + ", " + cmd3.rest()});
		}
		if (cmd1.is("INDEX") && 0 <= strToInt(cmd1.rest()) && strToInt(cmd1.rest()) <= 3 &&
			cmd2.is("INDEX") && 0 <= strToInt(cmd2.rest()) && strToInt(cmd2.rest()) <= 3) {
			return Result(true, 2, {"INDEX2 " + cmd1.rest() + ", " + cmd2.rest()});
		}
		if (cmd1.is_PUSHINT() && 0 <= cmd1.fetch_bigint() && cmd1.fetch_bigint() < (1LU << 11) &&
			cmd2.is("THROWANY")) {
			return Result(true, 2, {"THROW " + cmd1.rest()});
		}
		if (cmd1.is_PUSHINT() && 1 <= cmd1.fetch_bigint() && cmd1.fetch_bigint() <= 256 &&
			(cmd2.is("RSHIFT") || cmd2.is("LSHIFT"))) {
			return Result(true, 2, {cmd2.cmd_ + " " + cmd1.rest()});
		}

		std::map<bigint, int> map;
		for (int p2 = 2, p = 1; p2 <= 256; p2 *= 2, ++p) {
			map[p2] = p;
		}

		if (cmd1.is_PUSHINT() &&
			(cmd2.is("DIV") || cmd2.is("MUL"))) {
			bigint val = cmd1.fetch_bigint();
			if (map.count(val)) {
				const std::string& newOp = cmd2.is("DIV") ? "RSHIFT" : "LSHIFT";
				return Result(true, 2, {newOp + " " + toString(map.at(val))});
			}
		}
		if (cmd1.is_PUSHINT() &&
			cmd2.is("MOD")) {
			bigint val = cmd1.fetch_bigint();
			if (map.count(val)) {
				return Result(true, 2, {"MODPOW2 " + toString(map.at(val))});
			}
		}

		if (cmd1.is_PUSHINT()) {
			bigint val = cmd1.fetch_bigint();
			if (-128 <= val && val < 128) {
				if (cmd2.is("NEQ"))
					return Result(true, 2, {"NEQINT " + toString(val)});
				if (cmd2.is("EQUAL"))
					return Result(true, 2, {"EQINT " + toString(val)});
				if (cmd2.is("GREATER"))
					return Result(true, 2, {"GTINT " + toString(val)});
				if (cmd2.is("LESS"))
					return Result(true, 2, {"LESSINT " + toString(val)});
			}
		}

		if (cmd1.is("ROTREV") && cmd2.is("ROTREV") && cmd3.is("ROTREV")) {
			return Result(true, 3, {});
		}

		if (cmd1.is("BLKSWAP")) {
			int n = cmd1.fetch_first_int();
			bool ok = true;
			for (int iter = 0; iter < n + 1; ++iter) {
				Cmd c = cmd(idx1 + iter);
				ok &= c.is("BLKSWAP") && c.fetch_first_int() == n && c.fetch_second_int() == 1;
			}
			if (ok) {
				return Result(true, n + 1, {});
			}
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is_PUSHINT() &&
			cmd3.is_MUL()
		) {
			bigint a = cmd1.fetch_bigint();
			bigint b = cmd2.fetch_bigint();
			bigint c = a * b;
			return Result(true, 3, {"PUSHINT " + toString(c)});
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is_PUSHINT() &&
			cmd3.is_DIV()
		) {
			bigint a = cmd1.fetch_bigint();
			bigint b = cmd2.fetch_bigint();
			if (a >= 0 && b > 0) { // note in TVM  -9 / 2 == -5, TODO handle this cases
				bigint c = a / b;
				return Result(true, 3, {"PUSHINT " + toString(c)});
			}
		}

		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICE") &&
			cmd4.is("ENDC") &&
			cmd5.is("DROP")
		) {
			return Result(true, 5, {});
		}

		if (cmd1.is("XCHG") && cmd1.rest().find(',') == string::npos &&
			cmd2.is("BLKDROP") &&
			cmd3.is("NIP")
		) {
			int x = cmd1.fetchStackIndex();
			if (cmd2.get_drop_index() == x) {
				return Result(true, 3, {"XCHG s" + to_string(x + 1), "BLKDROP " + to_string(x + 1)});
			}
		}

		if (cmd1.is("BLKDROP2") &&
			cmd2.is("BLKDROP2")
		) {
			int f1 = cmd1.fetch_first_int();
			int f2 = cmd2.fetch_first_int();
			int s1 = cmd1.fetch_second_int();
			int s2 = cmd2.fetch_second_int();
			if (s1 == s2) {
				return Result(true, 2, {"BLKDROP2 " + to_string(f1 + f2) + ", " + to_string(s2) });
			}
		}

		if (cmd1.is("BLKSWAP") &&
			cmd2.is("BLKDROP")
		) {
			int a1 = cmd1.fetch_first_int();
			int b1 = cmd1.fetch_second_int();
			int a2 = cmd2.fetch_int();
			if (a1 == a2) {
				return Result(true, 2, {"BLKDROP2 " + to_string(a1) + ", " + to_string(b1) });
			}
		}

		if (cmd1.is("BLKDROP2") &&
			cmd2.is("BLKDROP2")
		) {
			int i1 = cmd1.fetch_first_int();
			int j1 = cmd1.fetch_second_int();
			int i2 = cmd2.fetch_first_int();
			int j2 = cmd2.fetch_second_int();
			if (j1 == i2 + j2) {
				return Result(true, 2, {"BLKDROP2 " + to_string(i1 + i2) + ", " + to_string(j2) });
			}
		}

        if (cmd1.is("MUL") && cmd2.is("RSHIFT")) {
            // RSHIFT can have parameter or can omit it
            return Result(true, 2, {"MULRSHIFT " + cmd2.rest_});
        }

        if (cmd1.is("NEWC") && cmd2.is("ENDC")) {
            return Result(true, 2, {"PUSHREF {", "}"});
        }

		if (cmd1.is("POP") &&
			cmd2.is("POP") &&
			cmd3.is("POP") &&
			cmd1.get_pop_index() == 3 &&
			cmd2.get_pop_index() == 3 &&
			cmd3.get_pop_index() == 3
		) {
			return Result(true, 3, {"BLKDROP2 3, 3"});
		}

		if (cmd1.is("ISNULL") &&
			cmd2.is("NOT") &&
			cmd3.is("NOT")
		) {
			return Result(true, 3, {"ISNULL"});
		}

		if (cmd1.is_PUSHINT() && cmd1.is_PUSHINT() && cmd1.fetch_bigint() == 0 &&
			cmd2.is("STUR") &&
			cmd3.is_PUSHINT() && cmd3.is_PUSHINT() && cmd3.fetch_bigint() == 0 &&
			cmd4.is("STUR")
		) {
			int bitSize = cmd2.fetch_int() + cmd4.fetch_int();
			if (bitSize <= 256)
				return Result(true, 4, {"PUSHINT 0", "STUR " + toString(bitSize)});
		}

		if ((cmd1.is("UFITS") && cmd2.is("UFITS"))  || (cmd1.is("FITS") && cmd2.is("FITS"))) {
			int bitSize = std::min(cmd1.fetch_int(), cmd2.fetch_int());
			return Result(true, 2, {cmd1.cmd_ + " " + toString(bitSize)});
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICECONST") &&
			cmd4.is("STU")) {
			std::string bitStr = toBitString(cmd3.rest());
			StackPusherHelper::addBinaryNumberToString(bitStr, cmd1.fetch_bigint(), cmd4.fetch_int());
			std::vector<std::string> slices = unitBitString(bitStr, "");
			if (slices.size() == 1) {
				return Result(true, 4, {
					"PUSHSLICE " + slices.at(0),
					"NEWC",
					"STSLICE"
				});
			}
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is("PUSHSLICE") &&
			cmd3.is("NEWC") &&
			cmd4.is("STSLICE") &&
			cmd5.is("STU")
		) {
			std::string bitStr = toBitString(cmd2.rest());
			StackPusherHelper::addBinaryNumberToString(bitStr, cmd1.fetch_bigint(), cmd5.fetch_int());
			std::vector<std::string> slices = unitBitString(bitStr, "");
			if (slices.size() == 1) {
				return Result(true, 5, {
						"PUSHSLICE " + slices.at(0),
						"NEWC",
						"STSLICE"
				});
			}
		}

		if (cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICE") &&
			(cmd4.is("STONE") || cmd4.is("STZERO"))
		) {
			std::string bitStr = toBitString(cmd1.rest());
			bitStr += cmd4.is("STONE") ? "1" : "0";
			std::vector<std::string> slices = unitBitString(bitStr, "");
			if (slices.size() == 1) {
				return Result(true, 4, {
						"PUSHSLICE " + slices.at(0),
						"NEWC",
						"STSLICE"
				});
			}
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is("STZEROES") &&
			cmd3.is_PUSHINT() &&
			cmd4.is("STZEROES")
		) {
			int bitQty = cmd1.fetch_int() + cmd3.fetch_int();
			return Result(true, 4, {
					"PUSHINT " + toString(bitQty),
					"STZEROES"
			});
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is("STUR") &&
			cmd3.is_PUSHINT() &&
			cmd4.is("STUR")
		) {
			bigint a = cmd1.fetch_bigint();
			int lenA = cmd2.fetch_int();
			bigint b = cmd3.fetch_bigint();
			int lenB = cmd4.fetch_int();
			if (lenA + lenB <= 256) {
				bigint c = (a << lenB) + b;
				return Result(true, 4, {
						"PUSHINT " + toString(c),
						"STUR " + toString(lenA + lenB)
				});
			}
		}

		if (cmd1.is_PUSHINT() &&
			cmd2.is("STZEROES") &&
			cmd3.is("STSLICECONST") && cmd3.rest() == "1"
		) {
			int lenA = cmd1.fetch_int();
			if (lenA <= 256) {
				return Result(true, 3, {
						"PUSHINT 1",
						"STUR " + toString(lenA + 1)
				});
			}
		}

		if ((cmd1.is("TRUE") || cmd1.is("FALSE")) &&
			cmd2.is("STIR") && cmd2.fetch_int() == 1
		) {
			if (cmd1.is("FALSE"))
				return Result(true, 2, {"STZERO"});
			return Result(true, 2, {"STONE"});
		}

		if ((cmd1.is("STONE") || cmd1.is("STZERO"))) {
			int qty = 0;
			int i = idx1;
			std::string bits;
			while (
				i != -1 &&
				qty < TvmConst::MaxSTSLICECONST &&
				(cmd(i).is("STONE") || cmd(i).is("STZERO"))
			) {
				bits += cmd(i).is("STONE") ? "1" : "0";
				++qty;
				i = next_command_line(i);
			}
			if (qty >= 2) {
				std::vector<std::string> slices = unitBitString(bits, "");
				solAssert(slices.size() == 1, "");
				return Result(true, qty, {"STSLICECONST " + slices.at(0)});
			}

		}

		if (
			cmd1.is("PUSHSLICE") &&
			cmd2.is("NEWC") &&
			cmd3.is("STSLICE") &&
			cmd4.is("NEWC") &&
			cmd5.is("STSLICECONST") &&
			cmd6.is("STB")
		) {
			std::string str1 = toBitString(cmd1.rest());
			std::string str5 = toBitString(cmd5.rest());
			std::vector<std::string> slices = unitBitString(str5, str1);
			if (slices.size() == 1) {
				return Result(true, 6, {
					"PUSHSLICE " + slices.at(0),
					"NEWC",
					"STSLICE"
				});
			}
		}

		if (
			cmd1.is("STSLICECONST") && cmd1.rest_ == "0" &&
			cmd2.is_PUSHINT() && cmd2.fetch_bigint() == 0 &&
			cmd3.is("STUR")
		) {
			int bitQty = cmd3.fetch_int();
			return Result(true, 3, {
				"PUSHINT 0",
				"STUR " + toString(bitQty + 1)
			});
		}

		if (
			cmd1.is_PUSHINT() &&
			cmd2.is("STZEROES") &&
			cmd3.is_PUSHINT() &&
			cmd4.is("STUR")
		) {
			bigint bitQty = cmd1.fetch_bigint() + cmd4.fetch_bigint();
			if (bitQty <= 256) {
				return Result(true, 4, {
					"PUSHINT " + cmd3.rest_,
					"STUR " + toString(bitQty)
				});
			}
		}

		if (
			cmd1.is_PUSHINT() && cmd1.fetch_bigint() == 0 &&
			cmd2.is("STUR")
		) {
			return Result(true, 2, {
				"PUSHINT " + cmd2.rest_,
				"STZEROES"
			});
		}

		if (
			cmd1.is("ABS") &&
			cmd2.is("UFITS") && cmd2.fetch_int() == 256
		) {
			return Result(true, 2, {"ABS"});
		}

		if (
			cmd1.is_PUSHINT() && cmd1.fetch_bigint() == 1 &&
			cmd2.is("STZEROES")
		) {
			return Result(true, 2, {"STZERO"});
		}

		return Result(false);
	}

	// TODO move to common file. See also binaryStringToSlice
	static std::string toBitString(const std::string& slice) {
		std::string bitString;
		if (slice.at(0) == 'x') {
			for (std::size_t i = 1; i < slice.size(); ++i) {
				if (i + 2 == slice.size() && slice[i + 1] == '_') {
					size_t pos{};
					int value = std::stoi(slice.substr(i, 1), &pos, 16);
					solAssert(pos == 1, "");
					int bitLen = 4;
					while (true) {
						bool isOne = value % 2 == 1;
						--bitLen;
						value /= 2;
						if (isOne) {
							break;
						}
					}
					StackPusherHelper::addBinaryNumberToString(bitString, value, bitLen);
					break;
				}
				size_t pos{};
				int value = std::stoi(slice.substr(i, 1), &pos, 16);
				solAssert(pos == 1, "");
				StackPusherHelper::addBinaryNumberToString(bitString, value, 4);
			}
		} else {
			if (isIn(slice, "0", "1")) {
				return slice;
			}
			solUnimplemented("");
		}
		return bitString;
	}

	static std::vector<std::string> unitSlices(const std::string& sliceA, const std::string& sliceB) {
		return unitBitString(toBitString(sliceA), toBitString(sliceB));
	}

	static std::vector<std::string> unitBitString(const std::string& bitStringA, const std::string& bitStringB) {
		const std::string& bitString = bitStringA + bitStringB;
		std::vector<std::string> opcodes;
		for (int i = 0; i < static_cast<int>(bitString.length()); i += 4 * TvmConst::MaxPushSliceBitLength) {
			opcodes.push_back(bitString.substr(i, std::min(4 * TvmConst::MaxPushSliceBitLength, static_cast<int>(bitString.length()) - i)));
		}
		for (std::string& opcode : opcodes) {
			opcode = "x" + StackPusherHelper::binaryStringToSlice(opcode);
		}
		return opcodes;
	}

	bool try_simulate(int i, int stack_size, int& remove_count, vector<string>& commands) const {
		if (!valid(i))
			return false;
		bool first_time = true;
		while (true) {
			if (first_time) {
				first_time = false;
			} else {
				remove_count++;
				i = next_command_line(i);
			}
			if (!valid(i))
				return false;
			Cmd c = cmd(i);
			// DBG(c.without_prefix() << " - " << stack_size);
			if (c.is_PUSH()) {
				if (c.get_push_index() + 1 == stack_size)
					return false;
				if (c.get_push_index() + 1 < stack_size) {
					commands.push_back(c.without_prefix());
				} else if (c.get_push_index() + 1 > stack_size) {
					if (c.get_push_index() == 0)
						return false;	// TODO: this should not happen...
					commands.push_back(make_PUSH(c.get_push_index()-1));
				}
				stack_size++;
				continue;
			}
			if (c.is_POP()) {
				if (stack_size == 1)
					return false;
				if (c.get_pop_index() + 1 == stack_size)
					return false;
				if (c.get_pop_index() + 1 < stack_size) {
					commands.push_back(c.without_prefix());
				} else if (c.get_pop_index() + 1 > stack_size) {
					commands.push_back(make_POP(c.get_pop_index()-1));
				}
				stack_size--;
				continue;
			}
			if (c.is("BLKPUSH")) {
				// check if the topmost element is not touched
				if (c.fetch_second_int() + 1 < stack_size) {
					commands.push_back(c.without_prefix());
					stack_size += c.fetch_first_int();
					continue;
				}
				// TODO: support other pushes not touching topmost element...
				return false;
			}
			if (c.is_NIP()) {
				if (stack_size == 2) {
					remove_count++;
					break;
				}
				if (stack_size > 2) {
					stack_size--;
					commands.push_back(c.without_prefix());
					continue;
				}
				return false;
			}
			if (c.is_drop_kind()) {
				int n = c.get_drop_index();
				if (stack_size <= n) {
					if (n > 1) {
						std::vector<std::string> dropOpcodes = make_DROP(n - 1);
						commands.insert(commands.end(), dropOpcodes.begin(), dropOpcodes.end());
					}
					remove_count++;
					break;
				} else {
					commands.push_back(c.without_prefix());
					stack_size -= n;
					continue;
				}
			}
			if (c.is_simple_command_) {
				if (stack_size <= c.inputs_count_)
					return false;
				commands.push_back(c.without_prefix());
				stack_size += c.outputs_count_ - c.inputs_count_;
				continue;
			}
			return false;
		}
		return true;
	}

	Result unsquash_push(const int idx1) const {
		Cmd cmd1 = cmd(idx1);
		if (cmd1.is("PUSH2")) {
			auto [si, sj] = cmd1.get_push2_indexes();
			return Result::Replace(1, make_PUSH(si), make_PUSH(sj + 1));
		}
		return Result(false);
	}

	Result squash_push(const int idx1) const {
		int idx2 = next_command_line(idx1);
		int idx3 = next_command_line(idx2);
		Cmd cmd1 = cmd(idx1);
		Cmd cmd2 = cmd(idx2);
		Cmd cmd3 = cmd(idx3);
		if (cmd1.is_PUSH() && cmd2.is_PUSH() && cmd3.is_PUSH()) {
			const int si = cmd1.get_push_index();
			const int sj = cmd2.get_push_index() - 1 == -1? si : cmd2.get_push_index() - 1;
			const int sk = cmd3.get_push_index() - 2 == -1? si : (
					cmd3.get_push_index() - 2 == -2? sj : cmd3.get_push_index() - 2
			);
			if (si <= 15 && sj <= 15 && sk <= 15) {
				const std::string newOpcode = (boost::format("PUSH3 S%d, S%d, S%d") % si % sj % sk).str();
				return Result::Replace(3, newOpcode);
			}
		}
		if (cmd1.is_PUSH() && cmd2.is_PUSH()) {
			if (cmd1.get_push_index() == 1 && cmd2.get_push_index() == 1) {
				return Result::Replace(2, "DUP2");
			}
			const int si = cmd1.get_push_index();
			const int sj = cmd2.get_push_index() - 1 == -1? si : cmd2.get_push_index() - 1;
			if (si <= 15 && sj <= 15) {
				const std::string newOpcode = (boost::format("PUSH2 S%d, S%d") % si % sj).str();
				return Result::Replace(2, newOpcode);
			}
		}
		if (cmd1.is("BLKPUSH")) {
			if (cmd1.fetch_first_int() == 2 && cmd1.fetch_second_int() == 1) {
				return Result::Replace(1, "DUP2");
			}
		}
		if (cmd1.is("BLKPUSH")) {
			if (cmd1.fetch_first_int() == 2 && cmd1.fetch_second_int() == 3) {
				return Result::Replace(1, "OVER2");
			}
		}
		return Result(false);
	}

	static std::vector<std::string> make_DROP(int n) {
		solAssert(n > 0, "");
		if (n == 1) return {"DROP"};
		if (n == 2) return {"DROP2"};
		if (n <= 15) return {"BLKDROP " + toString(n)};
		return {"PUSHINT " + toString(n), "DROPX"};
	}

	static string make_PUSH(int n) {
		solAssert(n >= 0, "");
		if (n == 0) return "DUP";
		return "PUSH S" + toString(n);
	}

	static string make_POP(int n) {
		solAssert(n >= 0, "");
		if (n == 0) return "DROP";
		if (n == 1) return "NIP";
		return "POP S" + toString(n);
	}

	static string make_BLKPUSH(int n, int m) {
		solAssert(n > 0, "");
		solAssert(m >= 0 && m <= 15, "");
		if (n == 1) return make_PUSH(m);
		return "BLKPUSH " + toString(n) + ", " + toString(m);
	}

	bool updateLines(int& idx1, const Result& res) {
		deque<int> linesToRemove;
		for (int i = idx1; linesToRemove.size() < size_t(res.remove_); i = next_command_line(i)) {
			linesToRemove.push_front(i);
		}

		if (!res.commands_.empty()) {
			string prefix = Cmd(lines_[idx1]).prefix_;
			for (int i = idx1, iter = 0; iter < res.remove_; i = next_command_line(i), ++iter) {
				string currentPrefix = Cmd(lines_[i]).prefix_;
				if (prefix.size() > currentPrefix.size()) {
					prefix = currentPrefix;
				}
			}

			if (!linesToRemove.empty()) {
				// We have removed something, so add replacement commands.
				for (auto it = res.commands_.rbegin(); it != res.commands_.rend(); it++)
					if (!it->empty()) {
						insert(linesToRemove.front() + 1, *it, prefix);
					}
			} else {
				// We add only a comment, check if it was not added before.
				solAssert(res.commands_.size() == 1, "");
				string cmd = res.commands_.front();
				if (lines_[idx1] != prefix + cmd) {
					if (lines_[idx1-1] != prefix + cmd) {
						insert(idx1, cmd, prefix);
						idx1++;
					}
				}
			}
		}

		if (false && !linesToRemove.empty()) {
			DBG("> Replacing");
			for (auto it = linesToRemove.rbegin(); it != linesToRemove.rend(); it++)
				DBG(lines_[*it]);
			DBG("> with");
			for (const auto& s : res.commands_)
				DBG(s);
		}

		for (int l : linesToRemove) {
			remove(l);
		}

		if (res.continue_) {
			// step back to several commands
			int cnt = 10, i = idx1;
			while (cnt > 0) {
				i--;
				if (!valid(i)) break;
				if (!is_comment_or_empty_line(lines_[i])) cnt--;
				idx1 = i;
			}
			return true;
		}

		return false;
	}

	void optimize(const std::function<Result(int)> &f) {
		int idx1 = 0;
		while (valid(idx1)) {
			Result res = f(idx1);
			if (updateLines(idx1, res)) {
				continue;
			}
			idx1 = next_command_line(idx1);
		}
	}
};

CodeLines optimize_code(const CodeLines& code0) {
	auto code = code0;
	TVMOptimizer optimizer{code.lines};
	optimizer.optimize([&optimizer](int index){ return optimizer.unsquash_push(index);});
	optimizer.optimize([&optimizer](int index){ return optimizer.optimize_at(index);});
	optimizer.optimize([&optimizer](int index){ return optimizer.optimize_at(index);});
	optimizer.optimize([&optimizer](int index){ return optimizer.squash_push(index);});
	code.lines = optimizer.lines_;
	return code;
}

void run_peephole_pass(const string& filename) {
	ifstream file(filename);
	string line;
	CodeLines code;
	while (getline(file, line)) {
		while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
			line.pop_back();
		code.push(line);
	}

	code = optimize_code(code);

	cout << code.str();
}

} // end solidity::frontend
