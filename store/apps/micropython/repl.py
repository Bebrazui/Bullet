# Interactive REPL Server
def run_code(code_str):
    try:
        return eval(code_str)
    except Exception as e:
        return str(e)
