# Generates the launcher/config UI icons in assets/icons/ (the 11 SVGs
# RecompFrontend loads by name): white, angular -- chamfered corners and square
# ends -- to suit the game.
#
#   python3 tools/make_icons.py [output dir, default assets/icons]
import math, os, sys
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'assets', 'icons')
W = 'fill="#FFFFFF"'
S = lambda w: f'stroke="#FFFFFF" stroke-width="{w}" stroke-linecap="butt" stroke-linejoin="miter" fill="none"'

def svg(w, h, body):
    return f'<svg width="{w}" height="{h}" viewBox="0 0 {w} {h}" fill="none" xmlns="http://www.w3.org/2000/svg">\n{body}\n</svg>\n'

def keyboard(x0, y0, w, h):
    # Chamfered plate with key holes cut out (evenodd).
    c = 2
    x1, y1 = x0 + w, y0 + h
    d = f'M{x0+c} {y0}H{x1-c}L{x1} {y0+c}V{y1-c}L{x1-c} {y1}H{x0+c}L{x0} {y1-c}V{y0+c}Z'
    k = 2.5   # key size
    g = 1.25  # gap
    # three rows: 6 keys, 6 keys, then 1 key + space bar + 1 key
    pitch = k + g
    row_w = 6 * k + 5 * g
    sx = x0 + (w - row_w) / 2
    ry = [y0 + (h - (3 * k + 2 * g)) / 2 + i * pitch for i in range(3)]
    for r in range(2):
        for i in range(6):
            x = sx + i * pitch
            d += f'M{x:.2f} {ry[r]:.2f}h{k}v{k}h-{k}Z'
    d += f'M{sx:.2f} {ry[2]:.2f}h{k}v{k}h-{k}Z'
    d += f'M{sx+pitch:.2f} {ry[2]:.2f}h{4*k+3*g:.2f}v{k}h-{4*k+3*g:.2f}Z'
    d += f'M{sx+5*pitch:.2f} {ry[2]:.2f}h{k}v{k}h-{k}Z'
    return f'<path fill-rule="evenodd" d="{d}" {W}/>'

icons = {}

icons['Caret.svg'] = svg(32, 32, f'<path d="M3 9L16 22L29 9V15.5L16 28.5L3 15.5Z" {W}/>')

icons['X.svg'] = svg(32, 32,
    f'<path d="M5 9L9 5L16 12L23 5L27 9L20 16L27 23L23 27L16 20L9 27L5 23L12 16Z" {W}/>')

icons['Trash.svg'] = svg(32, 32,
    f'<path d="M12 2H20V5H28V8.5H4V5H12Z" {W}/>\n'
    f'<path fill-rule="evenodd" d="M6.5 10.5H25.5L24 30H8ZM10.75 13.5V27H13.25V13.5ZM14.75 13.5V27H17.25V13.5ZM18.75 13.5V27H21.25V13.5Z" {W}/>')

icons['Reset.svg'] = svg(32, 32,
    f'<path d="M16 6A10 10 0 1 0 26 16" {S(4)}/>\n'
    f'<path d="M14 0.5L22 6L14 11.5Z" {W}/>')

icons['Quit.svg'] = svg(32, 32,
    f'<path d="M18 5H6V27H18" {S(4)}/>\n'
    f'<path d="M11 14H21V8.5L29.5 16L21 23.5V18H11Z" {W}/>')

# "?" with a square dot
cx, cy, r = 16, 11, 6
a = math.radians(50)
ex, ey = cx + r * math.cos(a), cy + r * math.sin(a)
icons['Question.svg'] = svg(32, 32,
    f'<path d="M10 11A6 6 0 1 1 {ex:.2f} {ey:.2f}L16 19.5V23" {S(4)}/>\n'
    f'<path d="M14 25.5H18V29.5H14Z" {W}/>')

# N64-style three-pronged controller, buttons cut out
cont = ('M5 6H27L30 9V16.5L28.5 27H23.5L21 18H19.5V25H12.5V18H11L8.5 27H3.5L2 16.5V9Z'
        'M6.75 8.5H9.25V10.25H11V12.75H9.25V14.5H6.75V12.75H5V10.25H6.75Z'
        'M21.5 8.5H24V11H21.5ZM24.75 11.75H27.25V14.25H24.75Z'
        'M14.75 11H17.25V13.5H14.75Z')
icons['Cont.svg'] = svg(32, 32, f'<path fill-rule="evenodd" d="{cont}" {W}/>')

icons['Keyboard.svg'] = svg(32, 32, keyboard(1, 8, 30, 16))

icons['PlusKeyboard.svg'] = svg(48, 20,
    f'<path d="M6.5 4H9.5V8.5H14V11.5H9.5V16H6.5V11.5H2V8.5H6.5Z" {W}/>\n' + keyboard(17, 1, 30, 18))

# Octagonal ring for the "press a button" border
pts = []
for i in range(8):
    t = math.radians(22.5 + 45 * i)
    pts.append(f'{20 + 18 * math.cos(t):.2f} {20 + 18 * math.sin(t):.2f}')
icons['RecordBorder.svg'] = svg(40, 40, f'<path d="M{"L".join(pts)}Z" {S(3)}/>')

# Spinner: a 270-degree arc (rotated by the UI while waiting)
icons['RecordSpinner.svg'] = svg(32, 32, f'<path d="M16 4A12 12 0 1 1 4 16" {S(4)}/>')

os.makedirs(out, exist_ok=True)
for name, s in icons.items():
    open(os.path.join(out, name), 'w').write(s)
print(len(icons), 'icons')
