// ============================================================================
// Comparative Virtual Machine Engine
// Stack-Based vs Register-Based Architecture — Single-file C++17
//
// Flow:  User input  →  Tokenizer  →  Shunting-Yard Compiler
//        →  Stack VM bytecode  →  Execute & visualize
//        →  Register VM instructions  →  Execute & visualize
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iomanip>
#include <cctype>
#include <algorithm>

// ============================================================================
// Token representation for the expression parser
// ================================================*============================

enum class TokenType { NUMBER, OPERATOR, LPAREN, RPAREN };

struct Token {
    TokenType   type;
    int         value = 0;    // valid when type == NUMBER
    char        op    = '\0'; // valid when type == OPERATOR
};

// ============================================================================
// Tokenizer — splits a raw math string into a token stream
// Handles multi-digit numbers, +, -, *, /, and parentheses.
// Recognizes unary minus at start of expression or after '(' or another op.
// ============================================================================

static std::vector<Token> tokenize(const std::string& expr) {
    std::vector<Token> tokens;
    const int len = static_cast<int>(expr.size());

    for (int i = 0; i < len; ) {
        if (std::isspace(expr[i])) { ++i; continue; }

        if (std::isdigit(expr[i])) {
            int num = 0;
            while (i < len && std::isdigit(expr[i]))
                num = num * 10 + (expr[i++] - '0');
            tokens.push_back({TokenType::NUMBER, num, '\0'});

        } else if (expr[i] == '(') {
            tokens.push_back({TokenType::LPAREN});
            ++i;

        } else if (expr[i] == ')') {
            tokens.push_back({TokenType::RPAREN});
            ++i;

        } else if (expr[i] == '+' || expr[i] == '-' ||
                   expr[i] == '*' || expr[i] == '/') {
            // Detect unary minus: at start, or after '(' or another operator
            if (expr[i] == '-' &&
                (tokens.empty() ||
                 tokens.back().type == TokenType::LPAREN ||
                 tokens.back().type == TokenType::OPERATOR)) {
                ++i;
                if (i < len && std::isdigit(expr[i])) {
                    int num = 0;
                    while (i < len && std::isdigit(expr[i]))
                        num = num * 10 + (expr[i++] - '0');
                    tokens.push_back({TokenType::NUMBER, -num, '\0'});
                } else {
                    throw std::runtime_error("Invalid expression after unary '-'");
                }
            } else {
                tokens.push_back({TokenType::OPERATOR, 0, expr[i]});
                ++i;
            }

        } else {
            throw std::runtime_error(
                std::string("Unexpected character: ") + expr[i]);
        }
    }
    return tokens;
}

// ============================================================================
// Shunting-Yard Algorithm — tokens → postfix (Reverse Polish Notation)
// ============================================================================

static int precedence(char op) {
    if (op == '+' || op == '-') return 1;
    if (op == '*' || op == '/') return 2;
    return 0;
}

static std::vector<Token> shuntingYard(const std::vector<Token>& tokens) {
    std::vector<Token> output;
    std::vector<Token> opStack;

    for (const auto& tok : tokens) {
        switch (tok.type) {

        case TokenType::NUMBER:
            output.push_back(tok);
            break;

        case TokenType::OPERATOR:
            while (!opStack.empty() &&
                   opStack.back().type == TokenType::OPERATOR &&
                   precedence(opStack.back().op) >= precedence(tok.op)) {
                output.push_back(opStack.back());
                opStack.pop_back();
            }
            opStack.push_back(tok);
            break;

        case TokenType::LPAREN:
            opStack.push_back(tok);
            break;

        case TokenType::RPAREN:
            while (!opStack.empty() &&
                   opStack.back().type != TokenType::LPAREN) {
                output.push_back(opStack.back());
                opStack.pop_back();
            }
            if (opStack.empty())
                throw std::runtime_error("Mismatched parentheses");
            opStack.pop_back(); // discard '('
            break;
        }
    }

    while (!opStack.empty()) {
        if (opStack.back().type == TokenType::LPAREN)
            throw std::runtime_error("Mismatched parentheses");
        output.push_back(opStack.back());
        opStack.pop_back();
    }
    return output;
}

// ============================================================================
// Stack VM Bytecode — one instruction per token
// ============================================================================

struct StackInstr {
    std::string opcode;  // "PUSH", "ADD", "SUB", "MUL", "DIV"
    int         operand = 0;
};

static std::vector<StackInstr> compileToStackBytecode(
        const std::vector<Token>& postfix) {
    std::vector<StackInstr> code;
    for (const auto& tok : postfix) {
        if (tok.type == TokenType::NUMBER) {
            code.push_back({"PUSH", tok.value});
        } else {
            switch (tok.op) {
                case '+': code.push_back({"ADD"}); break;
                case '-': code.push_back({"SUB"}); break;
                case '*': code.push_back({"MUL"}); break;
                case '/': code.push_back({"DIV"}); break;
                default:
                    throw std::runtime_error(
                        std::string("Unknown operator: ") + tok.op);
            }
        }
    }
    return code;
}

// ============================================================================
// 1. Custom LIFO Stack — raw fixed-size array, no std::stack
// ============================================================================

class LIFOStack {
    static constexpr int MAX_SIZE = 10;

    int data[MAX_SIZE]{};
    int top = -1;

public:
    [[nodiscard]] bool isEmpty() const noexcept { return top == -1; }
    [[nodiscard]] bool isFull()  const noexcept { return top == MAX_SIZE - 1; }

    void push(int val) {
        if (isFull())
            throw std::overflow_error("Stack Overflow");
        data[++top] = val;
    }

    int pop() {
        if (isEmpty())
            throw std::underflow_error("Stack Underflow");
        return data[top--];
    }

    [[nodiscard]] int peek() const {
        if (isEmpty())
            throw std::underflow_error("Stack Underflow");
        return data[top];
    }

    void display() const {
        std::cout << "  Stack  [ ";
        for (int i = 0; i <= top; ++i) {
            if (i > 0) std::cout << " | ";
            std::cout << data[i];
        }
        if (isEmpty()) std::cout << "empty";
        std::cout << " ]\n";
    }
};

// ============================================================================
// 2. Stack CPU — executes stack bytecode with step-by-step visualization
// ============================================================================

class StackCPU {
    LIFOStack stack;

public:
    int execute(const std::vector<StackInstr>& bytecode) {
        for (const auto& instr : bytecode) {

            // Format the instruction label
            std::string label = instr.opcode;
            if (instr.opcode == "PUSH")
                label += " " + std::to_string(instr.operand);

            // Dispatch
            if (instr.opcode == "PUSH") {
                stack.push(instr.operand);

            } else if (instr.opcode == "ADD") {
                int b = stack.pop(), a = stack.pop();
                stack.push(a + b);

            } else if (instr.opcode == "SUB") {
                int b = stack.pop(), a = stack.pop();
                stack.push(a - b);

            } else if (instr.opcode == "MUL") {
                int b = stack.pop(), a = stack.pop();
                stack.push(a * b);

            } else if (instr.opcode == "DIV") {
                if (stack.peek() == 0)
                    throw std::runtime_error("Divide by Zero");
                int b = stack.pop(), a = stack.pop();
                stack.push(a / b);
            }

            // Visual output
            std::cout << "  > " << std::left << std::setw(14) << label;
            stack.display();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return stack.peek();
    }
};

// ============================================================================
// 3. Register VM Instructions
// ============================================================================

enum class RegOp { LOAD, ADD, SUB, MUL, DIV };

struct RegInstr {
    RegOp op;
    int   dst;           // destination register index
    int   srcA  = -1;    // source register A  (or immediate for LOAD)
    int   srcB  = -1;    // source register B
};

// Compile postfix tokens into 3-address register instructions.
// Uses a simple virtual-register allocator: a stack of register indices
// tracks where each intermediate value lives.

static std::vector<RegInstr> compileToRegisterCode(
        const std::vector<Token>& postfix) {
    std::vector<RegInstr> code;
    std::vector<int> regStack;    // maps postfix operand slots → register ids
    int nextReg = 0;

    for (const auto& tok : postfix) {
        if (tok.type == TokenType::NUMBER) {
            int r = nextReg++;
            // For LOAD: dst = target register, srcA = immediate value
            code.push_back({RegOp::LOAD, r, tok.value, -1});
            regStack.push_back(r);
        } else {
            if (regStack.size() < 2)
                throw std::underflow_error("Register Underflow");
            int rb = regStack.back(); regStack.pop_back();
            int ra = regStack.back(); regStack.pop_back();
            int rd = nextReg++;

            RegOp op;
            switch (tok.op) {
                case '+': op = RegOp::ADD; break;
                case '-': op = RegOp::SUB; break;
                case '*': op = RegOp::MUL; break;
                case '/': op = RegOp::DIV; break;
                default:
                    throw std::runtime_error(
                        std::string("Unknown operator: ") + tok.op);
            }
            code.push_back({op, rd, ra, rb});
            regStack.push_back(rd);
        }
    }
    return code;
}

// ============================================================================
// 4. Register CPU — executes register instructions with visualization
// ============================================================================

class RegisterCPU {
    static constexpr int NUM_REGS = 16;
    int  regs[NUM_REGS]{};
    bool used[NUM_REGS]{};       // tracks which registers hold a value
    int  highWater = 0;          // highest register index written + 1

    // Format a human-readable instruction string
    static std::string formatInstr(const RegInstr& instr) {
        auto R = [](int i){ return "R" + std::to_string(i); };
        std::ostringstream ss;
        switch (instr.op) {
            case RegOp::LOAD:
                ss << "LOAD " << R(instr.dst) << ", " << instr.srcA;
                break;
            case RegOp::ADD:
                ss << "ADD  " << R(instr.dst)
                   << ", " << R(instr.srcA) << ", " << R(instr.srcB);
                break;
            case RegOp::SUB:
                ss << "SUB  " << R(instr.dst)
                   << ", " << R(instr.srcA) << ", " << R(instr.srcB);
                break;
            case RegOp::MUL:
                ss << "MUL  " << R(instr.dst)
                   << ", " << R(instr.srcA) << ", " << R(instr.srcB);
                break;
            case RegOp::DIV:
                ss << "DIV  " << R(instr.dst)
                   << ", " << R(instr.srcA) << ", " << R(instr.srcB);
                break;
        }
        return ss.str();
    }

    void displayRegs() const {
        std::cout << "  Regs   [ ";
        for (int i = 0; i < highWater; ++i) {
            if (i > 0) std::cout << " | ";
            std::cout << "R" << i << "=";
            if (used[i]) std::cout << regs[i];
            else         std::cout << "_";
        }
        std::cout << " ]\n";
    }

public:
    int execute(const std::vector<RegInstr>& code) {
        int lastDst = 0;

        for (const auto& instr : code) {
            if (instr.dst >= NUM_REGS)
                throw std::overflow_error("Register file exhausted");

            switch (instr.op) {
            case RegOp::LOAD:
                regs[instr.dst] = instr.srcA;    // srcA holds the immediate
                break;
            case RegOp::ADD:
                regs[instr.dst] = regs[instr.srcA] + regs[instr.srcB];
                break;
            case RegOp::SUB:
                regs[instr.dst] = regs[instr.srcA] - regs[instr.srcB];
                break;
            case RegOp::MUL:
                regs[instr.dst] = regs[instr.srcA] * regs[instr.srcB];
                break;
            case RegOp::DIV:
                if (regs[instr.srcB] == 0)
                    throw std::runtime_error("Divide by Zero");
                regs[instr.dst] = regs[instr.srcA] / regs[instr.srcB];
                break;
            }

            used[instr.dst] = true;
            if (instr.dst + 1 > highWater)
                highWater = instr.dst + 1;
            lastDst = instr.dst;

            // Visual output
            std::string label = formatInstr(instr);
            std::cout << "  > " << std::left << std::setw(22) << label;
            displayRegs();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return regs[lastDst];
    }
};

// ============================================================================
// Utility: section banners
// ============================================================================

static void banner(const std::string& title) {
    const std::string bar(50, '=');
    std::cout << '\n' << bar << '\n'
              << "  " << title << '\n'
              << bar << '\n';
}

static void subBanner(const std::string& title) {
    const std::string bar(50, '-');
    std::cout << '\n' << bar << '\n'
              << "  " << title << '\n'
              << bar << '\n';
}

// ============================================================================
// 5. main() — Presentation Flow
// ============================================================================

int main() {
    banner("Comparative VM Engine: Stack vs Register");

    // --- User input ---
    std::cout << "\n  Enter a math expression (e.g.  10 + 20 * 3 - 8 / 2 ):\n  >>> ";
    std::string expr;
    std::getline(std::cin, expr);

    if (expr.empty()) {
        std::cout << "  No input — using default: (10 + 20 * 3) - (8 / 2)\n";
        expr = "(10 + 20 * 3) - (8 / 2)";
    }

    // --- Front-end: Tokenize + Shunting-Yard → postfix ---
    std::vector<Token> tokens;
    std::vector<Token> postfix;
    try {
        tokens  = tokenize(expr);
        postfix = shuntingYard(tokens);
    } catch (const std::exception& e) {
        std::cerr << "\n  [PARSE ERROR] " << e.what() << '\n';
        return 1;
    }

    // Print the compiled postfix for clarity
    subBanner("Compiled Postfix (RPN)");
    std::cout << "  ";
    for (const auto& t : postfix) {
        if (t.type == TokenType::NUMBER) std::cout << t.value << ' ';
        else                             std::cout << t.op   << ' ';
    }
    std::cout << '\n';

    // =================================================================
    // A. STACK MACHINE
    // =================================================================
    subBanner("COMPILING AND EXECUTING ON STACK MACHINE");

    try {
        auto stackCode = compileToStackBytecode(postfix);

        // Show generated bytecode
        std::cout << "  Bytecode:  ";
        for (const auto& si : stackCode) {
            std::cout << si.opcode;
            if (si.opcode == "PUSH") std::cout << ' ' << si.operand;
            std::cout << "  ";
        }
        std::cout << "\n\n";

        StackCPU cpu;
        int result = cpu.execute(stackCode);
        std::cout << "\n  >>> Stack VM Result = " << result << "\n";

    } catch (const std::exception& e) {
        std::cerr << "\n  [STACK VM ERROR] " << e.what() << '\n';
    }

    // =================================================================
    // B. REGISTER MACHINE
    // =================================================================
    subBanner("COMPILING AND EXECUTING ON REGISTER MACHINE");

    try {
        auto regCode = compileToRegisterCode(postfix);

        // Show generated instructions
        std::cout << "  Instructions:\n";
        for (const auto& ri : regCode) {
            auto R = [](int i){ return "R" + std::to_string(i); };
            std::cout << "    ";
            switch (ri.op) {
                case RegOp::LOAD:
                    std::cout << "LOAD " << R(ri.dst) << ", " << ri.srcA;
                    break;
                case RegOp::ADD:
                    std::cout << "ADD  " << R(ri.dst) << ", "
                              << R(ri.srcA) << ", " << R(ri.srcB);
                    break;
                case RegOp::SUB:
                    std::cout << "SUB  " << R(ri.dst) << ", "
                              << R(ri.srcA) << ", " << R(ri.srcB);
                    break;
                case RegOp::MUL:
                    std::cout << "MUL  " << R(ri.dst) << ", "
                              << R(ri.srcA) << ", " << R(ri.srcB);
                    break;
                case RegOp::DIV:
                    std::cout << "DIV  " << R(ri.dst) << ", "
                              << R(ri.srcA) << ", " << R(ri.srcB);
                    break;
            }
            std::cout << '\n';
        }
        std::cout << '\n';

        RegisterCPU rcpu;
        int result = rcpu.execute(regCode);
        std::cout << "\n  >>> Register VM Result = " << result << "\n";

    } catch (const std::exception& e) {
        std::cerr << "\n  [REGISTER VM ERROR] " << e.what() << '\n';
    }


    std::cout << '\n' << std::string(50, '=') << '\n'
              << "  Demo complete.\n"
              << std::string(50, '=') << '\n';

    return 0;
}
