#pragma once
#include <optional>
#include <variant>
#include <iostream>
#include <cstring>
#include <string>
#include <string_view>
#include "../t86/program/programbuilder.h"
#include "../t86/cpu.h"
#include "../t86/program/helpers.h"
#include "../common/helpers.h"


class ParserError : std::exception {
public:
    ParserError(std::string message) : message(std::move(message)) { }
    const char* what() const noexcept override {
        return message.c_str();
    }
private:
    std::string message;
};

enum class Token {
    ID,
    DOT,
    NUM,
    NUM_FLOAT,
    LBRACKET,
    RBRACKET,
    END,
    SEMICOLON,
    PLUS,
    TIMES,
    COMMA,
};

struct SourceLocation {
    int line{1};
    int col{1};

    friend std::ostream& operator <<(std::ostream& os, SourceLocation loc) {
        utils::output(os, "{}:{}", loc.line, loc.col);
        return os;
    }
};

/// Parses text representation of T86 into in-memory representation
class Lexer {
public:
    explicit Lexer(std::istream& input) noexcept : input(input) { }

    Token getNext() {
        char c = getChar();
        
        if (c == '#') {
            while(c != EOF && c != '\n') {
                c = getChar();
            }
            return getNext();
        } else if (c == EOF) {
            return Token::END;
        } else if (isspace(c)) {
            return getNext();
        } else if (c == ';') {
            return Token::SEMICOLON;
        } else if (c == ',') {
            return Token::COMMA;
        } else if (c == '[') {
            return Token::LBRACKET;
        } else if (c == ']') {
            return Token::RBRACKET;
        } else if (c == '+') {
            return Token::PLUS;
        } else if (c == '*') {
            return Token::TIMES;
        } else if (c == '.') {
            return Token::DOT;
        } else if (isdigit(c) || c == '-') {
            int neg = c == '-' ? -1 : 1;
            if (neg == -1) {
                c = getChar();
            }
            // TODO: inf, nan
            bool has_dot = false, has_e = false; // 1.2e5
            std::string num{c};
            while (true) {
                c = getChar();
                if (c == '.') {
                    if(has_dot || has_e) {
                        throw ParserError(utils::format("Invalid floating point literal: {}{}", num, c));
                    }
                    has_dot = true;
                } else if(c == 'e' || c == 'E') {
                    if(has_e) {
                        throw ParserError(utils::format("Invalid floating point literal: {}{}", num, c));
                    }
                    has_e = true;
                } else if(c == '+' || c == '-') {
                    if(!has_e) {
                        throw ParserError(utils::format("Invalid floating point literal: {}{}", num, c));
                    }
                } else if (!isdigit(c)) {
                    undoGetChar();
                    break;
                }
                num += c;
            }
            if(has_dot || has_e) {
                float_number = neg * std::stod(num);
                return Token::NUM_FLOAT;
            } else {
                number = neg * std::stoll(num);
                return Token::NUM;
            }
        } else {
            std::string str{c};
            while (true) {
                c = getChar();
                if (!isalnum(c)) {
                    undoGetChar();
                    break;
                }
                str += c;
            }
            id = str;
            return Token::ID;
        }
    }

    std::string getId() const { return id; }
    int64_t getNumber() const noexcept { return number; }
    double getFloatNumber() const noexcept { return float_number; }

    SourceLocation getLocation() const { return loc; }
private:
    int64_t number{-1};
    double float_number{-1};
    std::string id;
    std::istream& input;
    SourceLocation prev_loc, loc;

    char getChar() {
        char c = input.get();
        prev_loc = loc;
        if(c == '\n') {
            loc = {loc.line + 1, 1};
        } else {
            loc.col++;
        }
        return c;
    }

    void undoGetChar() {
        input.unget();
        loc = prev_loc;
    }
};

class Parser {
public:
    Parser(std::istream& is) : lex(is) { curtok = lex.getNext(); }
    static void ExpectTok(Token expected, Token tok, std::function<std::string()> message) {
        if (expected != tok) {
            throw ParserError(message());
        }
    }
    // static void ExpectTok(Token expected, Token tok, const std::string& message) {
    //     if(expected != tok) {
    //         throw ParserError(message);
    //     }
    // }

    /// Fetches new token and returns previous (the one that was in curtok until this function call) one.
    Token GetNextPrev() {
        auto prev = curtok;
        curtok = lex.getNext();
        return prev;
    }

    Token GetNext() {
        return curtok = lex.getNext();
    }

    void Section() {
        ExpectTok(Token::ID, curtok, [&]{ return utils::format("[{}]: Expected '.section_name'", lex.getLocation()); });
        std::string section_name = lex.getId();
        GetNextPrev();
        if (section_name == "text") {
            Text();
        } else if(section_name == "data") {
            Data();
        } else {
            throw ParserError("Invalid section name");
        }
    }

    bool isFloatRegister(std::string_view regname) const {
        return regname.starts_with("FR");
    }

    tiny::t86::FloatRegister getFloatRegister(std::string_view regname) const {
        if (!regname.starts_with("FR")) {
            throw ParserError(utils::format("Float registers must begin with FR, got {}", regname));
        }
        regname.remove_prefix(2);
        return tiny::t86::FloatRegister{static_cast<size_t>(std::atoi(regname.data()))};
    }

    tiny::t86::Register getRegister(std::string_view regname) const {
        if (regname == "BP") {
            return tiny::t86::Register::StackBasePointer();
        } else if (regname == "SP") {
            return tiny::t86::Register::StackPointer();
        } else if (regname == "IP") {
            return tiny::t86::Register::ProgramCounter();
        } else if (regname[0] != 'R') {
            throw ParserError(utils::format("Registers must begin with an R, unless IP, BP or SP, got {}", regname));
        }
        regname.remove_prefix(1);
        return tiny::t86::Register{static_cast<size_t>(std::atoi(regname.data()))};
    }

    tiny::t86::Operand Operand() {
        using namespace tiny::t86;
        if (curtok == Token::ID) {
            std::string regname = lex.getId();
            GetNext();
            // Reg + Imm
            if (curtok == Token::PLUS) {
                if (GetNext() != Token::NUM) {
                    throw ParserError("After Reg + _ there can be only number");
                }
                auto imm = lex.getNumber();
                GetNext();
                return getRegister(regname) + imm;
            }
            if(isFloatRegister(regname)) {
                auto freg = getFloatRegister(regname);
                return freg;
            } else {
                auto reg = getRegister(regname);
                return reg;
            }
        } else if (curtok == Token::NUM) {
            auto imm = lex.getNumber();
            GetNext();
            return tiny::t86::Operand(imm);
        } else if (curtok == Token::NUM_FLOAT) {
            auto fimm = lex.getFloatNumber();
            GetNext();
            return tiny::t86::Operand(fimm);
        } else if (curtok == Token::LBRACKET) { // Mem
            GetNext(); // '['
            // First is imm
            if (curtok == Token::NUM) {
                auto val = lex.getNumber();
                GetNext(); // 'num'
                ExpectTok(Token::RBRACKET, curtok, []{ return "Expected ']' to close [Imm]\n"; });
                GetNext(); // ']'
                // [i]
                return Mem(static_cast<uint64_t>(val));
            // First is register
            } else if (curtok == Token::ID) {
                auto reg = getRegister(lex.getId());
                GetNext(); // reg

                if (curtok == Token::RBRACKET) { // Only register
                    GetNext(); // ']'
                    // [Rx]
                    return Mem(reg);
                } else if (curtok == Token::PLUS) { // Has second with +
                    GetNext();  // '+'
                    // Can either be imm or register
                    if (curtok == Token::ID) {
                        auto reg2 = getRegister(lex.getId());
                        GetNext(); // reg2

                        if (curtok == Token::RBRACKET) {
                            GetNext(); // ']'
                            return Mem(reg + reg2);
                        } else if (curtok == Token::TIMES) {
                            GetNext(); // '*'
                            ExpectTok(Token::NUM, curtok, []{ return "Expected Imm in [Reg + Reg * Imm]\n"; });
                            auto val = lex.getNumber();
                            GetNext(); // imm
                            ExpectTok(Token::RBRACKET, curtok, []{ return "Expected ']' to close [Reg + Reg * Imm]\n"; });
                            GetNext(); // ']'
                            return Mem(reg + reg2 * val);
                        }
                    } else if (curtok == Token::NUM) {
                        auto val = lex.getNumber();
                        if (GetNext() == Token::RBRACKET) {
                            GetNext();
                            return Mem(reg + val);
                        }
                        if (curtok != Token::PLUS || GetNext() != Token::ID) {
                            throw ParserError("Dereference of form [R1 + i ...] must always contain `+ R` after i");
                        }

                        auto reg2 = getRegister(lex.getId());
                        if (GetNext() == Token::RBRACKET) {
                            GetNext();
                            return Mem(reg + val + reg2);
                        }
                        ExpectTok(Token::TIMES, curtok, []{ return "After `[R1 + i + R2` there must always be a `*` or `]`"; });
                        ExpectTok(Token::NUM, GetNext(), []{ return "After `[R1 + i + R2 *` there must always be an imm"; });
                        auto val2 = lex.getNumber();
                        
                        ExpectTok(Token::RBRACKET, GetNext(), []{ return "Expected ']' to close dereference "; });
                        GetNext(); // Eat ']'
                        return Mem(reg + val + reg2 * val2);
                    }
                // Has second with *
                } else if (curtok == Token::TIMES) {
                    // There must be imm now
                    ExpectTok(Token::NUM, GetNext(), []{ return "After [R1 * ...] there must always be an imm"; });
                    int val = lex.getNumber();
                    if (GetNext() != Token::RBRACKET) {
                        throw ParserError("Expected ']' to close dereference");
                    }
                    GetNext(); // ']'
                    return Mem(reg * val);
                }
                UNREACHABLE;
            }
            NOT_IMPLEMENTED;
        }
        UNREACHABLE;
    }

    tiny::t86::Register Register() {
        auto op = Operand();
        if(!op.isRegister())
            throw ParserError(utils::format("[{}]: Expected Reg, got {} ({})", lex.getLocation(), tiny::t86::Operand::typeToString(op.getType()), op.toString()));
        return op.getRegister();
    }

    tiny::t86::FloatRegister FloatRegister() {
        auto op = Operand();
        if(!op.isFloatRegister())
            throw ParserError(utils::format("[{}]: Expected FReg, got {} ({})", lex.getLocation(), tiny::t86::Operand::typeToString(op.getType()), op.toString()));
        return op.getFloatRegister();
    }

#define CHECK_COMMA() do { ExpectTok(Token::COMMA, GetNextPrev(), []{ return "Expected comma to separate arguments"; });} while (false)

    tiny::t86::Instruction* Instruction() {
        // Address at the beginning is optional
        if (curtok == Token::NUM) {
            GetNextPrev();
        }

        ExpectTok(Token::ID, curtok, [&]{ return utils::format("[{}]: Expected register name", lex.getLocation()); });
        std::string ins_name = lex.getId();
        GetNextPrev();

        if (ins_name == "MOV") {
            auto dest = Operand();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::MOV{dest, from};
        } else if (ins_name == "ADD") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();

            return new tiny::t86::ADD{dest, from};
        } else if (ins_name == "LEA") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();

            return new tiny::t86::LEA(dest, from);
        } else if (ins_name == "HALT") {
            return new tiny::t86::HALT{};
        } else if (ins_name == "DBG") {
            // TODO: This probably won't be used anymore. It would be very difficult (impossible) to
            //       to pass lambda in text file
            throw ParserError("DBG instruction is not supported");
        } else if (ins_name == "BREAK") {
            return new tiny::t86::BREAK{};
        } else if (ins_name == "SUB") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::SUB{dest, from};
        } else if (ins_name == "INC") {
            auto op = Register();
            return new tiny::t86::INC{op};
        } else if (ins_name == "DEC") {
            auto op = Register();
            return new tiny::t86::DEC{op};
        } else if (ins_name == "NEG") {
            auto op = Register();
            return new tiny::t86::NEG{op};
        } else if (ins_name == "MUL") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::MUL{dest, from};
        } else if (ins_name == "DIV") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::DIV{dest, from};
        } else if (ins_name == "MOD") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::MOD{dest, from};
        } else if (ins_name == "IMUL") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::IMUL{dest, from};
        } else if (ins_name == "IDIV") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::IDIV{dest, from};
        } else if (ins_name == "AND") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::AND{dest, from};
        } else if (ins_name == "OR") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::OR{dest, from};
        } else if (ins_name == "XOR") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::XOR{dest, from};
        } else if (ins_name == "NOT") {
            auto op = Operand();
            return new tiny::t86::NOT{op.getRegister()};
        } else if (ins_name == "LSH") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::LSH{dest, from};
        } else if (ins_name == "RSH") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::RSH{dest, from};
        } else if (ins_name == "CLF") {
            throw ParserError("CLF instruction is not implemented");
        } else if (ins_name == "CMP") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::CMP{dest, from};
        } else if (ins_name == "FCMP") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Operand();
            if (from.isFloatValue()) {
                return new tiny::t86::FCMP{dest, from.getFloatValue()};
            } else if (from.isFloatRegister()) {
                return new tiny::t86::FCMP{dest, from.getFloatRegister()};
            } else {
                throw ParserError("FCMP must have either float value or float register as dest");
            }
        } else if (ins_name == "JMP") {
            auto dest = Operand();
            if (dest.isRegister()) {
                return new tiny::t86::JMP{dest.getRegister()};
            } else if (dest.isValue()) {
                return new tiny::t86::JMP{static_cast<uint64_t>(dest.getValue())};
            } else {
                throw ParserError("JMP must have either register or value as dest");
            }
        } else if (ins_name == "LOOP") {
            auto reg = Register();
            CHECK_COMMA();
            auto address = Operand();
            if (address.isRegister()) {
                return new tiny::t86::LOOP{reg, address.getRegister()};
            } else if (address.isValue()) {
                return new tiny::t86::LOOP{reg, static_cast<uint64_t>(address.getValue())};
            } else {
                throw ParserError("LOOP must have either register or value as dest");
            }
        } else if (ins_name == "JZ") {
            auto dest = Operand();
            return new tiny::t86::JZ(dest);
        } else if (ins_name == "JNZ") {
            auto dest = Operand();
            return new tiny::t86::JNZ(dest);
        } else if (ins_name == "JE") {
            auto dest = Operand();
            return new tiny::t86::JE(dest);
        } else if (ins_name == "JNE") {
            auto dest = Operand();
            return new tiny::t86::JNE(dest);
        } else if (ins_name == "JG") {
            auto dest = Operand();
            return new tiny::t86::JG(dest);
        } else if (ins_name == "JGE") {
            auto dest = Operand();
            return new tiny::t86::JGE(dest);
        } else if (ins_name == "JL") {
            auto dest = Operand();
            return new tiny::t86::JL(dest);
        } else if (ins_name == "JLE") {
            auto dest = Operand();
            return new tiny::t86::JLE(dest);
        } else if (ins_name == "JA") {
            auto dest = Operand();
            return new tiny::t86::JA(dest);
        } else if (ins_name == "JAE") {
            auto dest = Operand();
            return new tiny::t86::JAE(dest);
        } else if (ins_name == "JB") {
            auto dest = Operand();
            return new tiny::t86::JB(dest);
        } else if (ins_name == "JBE") {
            auto dest = Operand();
            return new tiny::t86::JBE(dest);
        } else if (ins_name == "JO") {
            auto dest = Operand();
            return new tiny::t86::JO(dest);
        } else if (ins_name == "JNO") {
            auto dest = Operand();
            return new tiny::t86::JNO(dest);
        } else if (ins_name == "JS") {
            auto dest = Operand();
            return new tiny::t86::JS(dest);
        } else if (ins_name == "JNS") {
            auto dest = Operand();
            return new tiny::t86::JNS(dest);
        } else if (ins_name == "CALL") {
            auto dest = Operand();
            return new tiny::t86::CALL(dest);
        } else if (ins_name == "RET") {
            return new tiny::t86::RET();
        } else if (ins_name == "PUSH") {
            auto val = Operand();
            return new tiny::t86::PUSH{val};
        } else if (ins_name == "FPUSH") {
            auto val = Operand();
            return new tiny::t86::FPUSH{val};
        } else if (ins_name == "POP") {
            auto reg = Register();
            return new tiny::t86::POP{reg};
        } else if (ins_name == "FPOP") {
            auto reg = FloatRegister();
            return new tiny::t86::FPOP{reg};
        } else if (ins_name == "GETCHAR") {
            auto reg = Register();
            return new tiny::t86::GETCHAR(reg, std::cin);
        } else if (ins_name == "PUTCHAR") {
            auto reg = Register();
            return new tiny::t86::PUTCHAR{reg, std::cout};
        } else if (ins_name == "PUTNUM") {
            auto reg = Register();
            return new tiny::t86::PUTNUM{reg, std::cout};
        } else if (ins_name == "FADD") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::FADD{dest, from};
        } else if (ins_name == "FSUB") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::FSUB{dest, from};
        } else if (ins_name == "FMUL") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::FMUL{dest, from};
        } else if (ins_name == "FDIV") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Operand();
            return new tiny::t86::FDIV{dest, from};
        } else if (ins_name == "EXT") {
            auto dest = FloatRegister();
            CHECK_COMMA();
            auto from = Register();
            return new tiny::t86::EXT{dest, from};
        } else if (ins_name == "NRW") {
            auto dest = Register();
            CHECK_COMMA();
            auto from = FloatRegister();
            return new tiny::t86::NRW{dest, from};
        } else if (ins_name == "NOP") {
            return new tiny::t86::NOP{};
        } else {
            throw ParserError(utils::format("Unknown instruction {}", ins_name));
        }
    }

#undef CHECK_COMMA

    void Text() {
        utils::logger("Parsing text section...");
        while (curtok == Token::NUM || curtok == Token::ID) {
            auto ins = Instruction();
            try {
                ins->validate();
            } catch(tiny::t86::Instruction::InvalidOperand &err) {
                throw ParserError(utils::format("[{}]: {}: {}", lex.getLocation(), tiny::t86::Instruction::typeToString(ins->type()), err.what()));
            }
            program.push_back(ins);
            // if (GetNextPrev() != Token::SEMICOLON) {
            //     throw ParserError("Instruction must be terminated by semicolon");
            // }
        }
    }

    void Data() {
        utils::logger("Parsing data section...");
        while (curtok == Token::NUM || curtok == Token::ID) {
            // Address at the beginning is optional
            if (curtok == Token::NUM) {
                GetNext();
            }

            ExpectTok(Token::ID, curtok, []{ return "Expected DW"; });
            std::string op_name = lex.getId();
            GetNext();

            if (op_name != "DW") {
                throw ParserError(utils::format("[{}] Expected DW", lex.getLocation()));
            }

            ExpectTok(Token::NUM, curtok, []{ return "Expected number after DW"; });
            auto word = lex.getNumber();
            GetNext();

            int64_t repCount = 1;
            if (curtok == Token::TIMES) {
                GetNext();
                ExpectTok(Token::NUM, curtok, []{ return "Expected number after *"; });
                repCount = lex.getNumber();
                GetNext();
            }

            for (int64_t i = 0; i < repCount; i++) {
                data.emplace_back(word);
            }
        }
    }

    tiny::t86::Program Parse() {
        if (curtok != Token::DOT) {
            throw ParserError("File does not contain any section");
        }
        while (GetNextPrev() == Token::DOT) {
            Section();
        }
        ExpectTok(Token::END, curtok, []{ return "Expected end of file"; });

        return { program, data };
    }
private:
    Lexer lex;
    Token curtok;
    std::vector<tiny::t86::Instruction*> program;
    std::vector<int64_t> data;
};

