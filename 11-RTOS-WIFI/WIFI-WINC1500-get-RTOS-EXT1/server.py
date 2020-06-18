from flask import Flask, render_template, request, jsonify
from flask_restful import Resource, Api

app = Flask(__name__)

global led
global ON
id = 0
ON = 0
VOL = 0
Hora = ""
@app.route('/')
def control():
   return render_template('index.html')

@app.route('/status', methods = ['POST', 'GET'])
def status():
   global id
   global ON
   global VOL
   global Hora
   if request.method == 'POST':
      status = request.form
      id = status['id']
      ON = status['ON']
      VOL = status['Volume']
      Hora = status['Hora']
      return jsonify({'id' : id, 'ON' : ON, 'Volume' : VOL, 'Hora': Hora}), 200
   else:
      return jsonify({'id' : id, 'ON' : ON, 'Volume' : VOL, 'Hora': Hora}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0',debug=True)
