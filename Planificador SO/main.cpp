#include <allegro5/allegro.h>
#include <allegro5/utf8.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_native_dialog.h>


#include <iostream>
#include <time.h>
#include <queue>
#include <vector>
#include <string>
using namespace std;

struct boton {
	float x1, y1, x2, y2;
	string texto;
	bool contiene(float x, float y) const {
		return x >= x1 && x <= x2 && y >= y1 && y <= y2;
	}
};

struct campoTxt {
	float x1, y1, x2, y2, x3;
	string texto, contenido;
	bool activo;
	bool contiene(float x, float y) const {
		return x >= x2 && x <= x3 && y >= y1 && y <= y2;
	}
};

struct Proceso {
	int ID;
	int tamano;
	int quantum;
};

struct recuadro {
	float x1, y1, x2, y2;
};

class Pantalla {
protected:
	ALLEGRO_DISPLAY* display;
	ALLEGRO_EVENT_QUEUE* event_queue;
	ALLEGRO_TIMER* timerSim = al_create_timer(1.0);
	int memoria;

	/* Variables para utilizar timer */
	bool ejecutando = false;
	const double velocidades[5] = {2.0,1.0, 0.5, 0.2, 0.1};
	int velocidadActual = 1; // indice de velocidades
public:
	Pantalla(ALLEGRO_DISPLAY *display, ALLEGRO_EVENT_QUEUE *event_queue) : display(display), event_queue(event_queue), memoria(0) {}

	ALLEGRO_DISPLAY *getDisplay() { return display; }
	ALLEGRO_EVENT_QUEUE* getEventQueue() { return event_queue; }
	bool getEjecutando() { return ejecutando; }
	virtual ~Pantalla() = default;
	virtual void evento(const ALLEGRO_EVENT& ev, Pantalla*& pantalla) = 0;
	virtual void dibujar(ALLEGRO_FONT* fuente) = 0;

	/* Funciones para utilizar timer en cualquier pestana */
	void activarTimer(ALLEGRO_TIMER*& timerSim) {
		if (ejecutando && !al_get_timer_started(timerSim))
			al_start_timer(timerSim);
	}
	ALLEGRO_TIMER* getTimer() { return timerSim; }
	int *getVelocidadActual() { return &velocidadActual; }
	double getVelocidad() { return velocidades[velocidadActual]; }
};

class PantallaUni : public Pantalla {
private:
	vector<Proceso> memoria = { { 0, 8192, 0 } }; // id = 0  Hueco
	vector<Proceso> roundRobin; // fila de listos
	vector<boton> botonesForm = { 
		{300, 100, 500, 150, "First Fit"},
		{530, 100, 730, 150, "Best Fit"}, 
		{300, 600, 500, 650, "Simular"}
	};
	vector<campoTxt> camposForm{
		{300, 300, 520, 350, 600, "Quantum del sistema", ""},
		{300, 400, 520, 450, 600, "Quantum del proceso", ""},
		{300, 500, 520, 550, 600, "Tam max del proceso", "" }
	};
	vector<boton> botonesEjecucion = {
		{ 300, 800, 500, 850, "Pausar" },
		{ 530, 800, 730, 850, "Reanudar" },
		{ 760, 800, 960, 850, "- Velocidad" },
		{ 990, 800, 1190, 850, "+ Velocidad" },
		{ 1220, 800, 1420, 850, "Terminar" }
	};
	recuadro recuadroMemoria = { 600, 150, 1400, 200 };

	int quantumSistema = 0, quantumProceso = 0, tamanoMax = 0, tamanoMem = 8192; //quantum proceso es tamano maximo de quantum por proceso. quantum sistema es lo que le damos de cpu a cada proceso
	int contAux = 0;
	bool firstBest = false; // false = First Fit, true = Best Fit

	/* Stats */
	int procesosAtendidos = 0, proSinEntrar = 0, proTotal = 0;
	double memOcupada = 0, memOcupPor = 0;

public:
	PantallaUni(ALLEGRO_DISPLAY* display, ALLEGRO_EVENT_QUEUE* event_queue) : Pantalla(display, event_queue) {}
	void descargar(int id) {
		for (size_t i = 0; i < memoria.size(); i++) {
			if (memoria[i].ID == id) {
				memoria[i].ID = 0;
				memoria[i].quantum = 0;
				break;
			}
		}
		condensar();
	}

	void condensar() {
		for (size_t i = 0; i + 1 < memoria.size(); ) {
			if (memoria[i].ID == 0 && memoria[i + 1].ID == 0) {
				memoria[i].tamano += memoria[i + 1].tamano;
				memoria.erase(memoria.begin() + i + 1);
			}
			else i++;
		}
	}
	void anadirACola(int id, int tamano, int quantum) {
		Proceso p = { id, tamano, quantum };
		roundRobin.push_back(p);
	}
	void turnoProceso() {
		if (roundRobin.empty()) return;
		Proceso aux = roundRobin.front();
		aux.quantum -= quantumSistema;
		roundRobin.erase(roundRobin.begin());
		if (aux.quantum > 0)
			roundRobin.push_back(aux);
		else {
			descargar(aux.ID);
			procesosAtendidos++;
		}
	}

	void firstFit(int id, int tamano, int quantum) {
		for (size_t i = 0; i < memoria.size(); i++) {
			if (memoria[i].ID == 0 && memoria[i].tamano >= tamano) {
				int sobrante = memoria[i].tamano - tamano;
				memoria[i] = { id, tamano, quantum };
				if (sobrante > 0)
					memoria.insert(memoria.begin() + i + 1, Proceso{ 0, sobrante, 0 });
				anadirACola(id, tamano, quantum);
				return;
			}
		}
		proSinEntrar++;
	}
	void bestFit(int id, int tamano, int quantum) {
		int mejorIndice = -1;
		int mejorTamano = tamanoMem + 1; // Inicializar con un tamaño mayor al máximo, si no cabe en memoria no se asigna
		for (int i = 0; i < memoria.size(); ++i) {
			if (memoria[i].ID == 0 && memoria[i].tamano >= tamano && memoria[i].tamano < mejorTamano) {
				mejorTamano = memoria[i].tamano;
				mejorIndice = i;
			}
		}
		if (mejorIndice != -1) {
			anadirACola(id, tamano, quantum);
			if (memoria[mejorIndice].tamano > tamano) {
				Proceso hueco = { 0, memoria[mejorIndice].tamano - tamano, 0 };
				memoria.insert(memoria.begin() + mejorIndice + 1, hueco);
			}
			memoria[mejorIndice].ID = id;
			memoria[mejorIndice].tamano = tamano;
			memoria[mejorIndice].quantum = quantum;
			return;
		}
		proSinEntrar++;
	}


	void evento(const ALLEGRO_EVENT& ev, Pantalla*& pantalla) override {
		if (!ejecutando) {
			if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
				float x = ev.mouse.x;
				float y = ev.mouse.y;
				for (auto& campo : camposForm) {
					if (campo.contiene(x, y)) {
						campo.activo = true;
					}
					else {
						campo.activo = false;
					}
				}
				for (auto& b : botonesForm) {
					if (b.contiene(x, y)) {
						if (b.texto == "Simular") {
							if (camposForm[0].contenido.empty() || camposForm[1].contenido.empty() || camposForm[2].contenido.empty()) {
								al_show_native_message_box(display, "Error", "Campos vacíos", "Llene los campos vacíos", NULL, ALLEGRO_MESSAGEBOX_ERROR);
								return;
							} 
							quantumSistema = stoi(camposForm[0].contenido);
							quantumProceso = stoi(camposForm[1].contenido);
							tamanoMax = stoi(camposForm[2].contenido);
							if (tamanoMax > tamanoMem) {
								al_show_native_message_box(display, "Error", "Tamaño máximo inválido", "El tamaño máximo del proceso no puede ser mayor al tamaño de la memoria", NULL, ALLEGRO_MESSAGEBOX_ERROR);
								return;
							}
							if (quantumProceso <= 0 || quantumSistema <= 0 || tamanoMax <= 0) {
								al_show_native_message_box(display, "Error", "Valores inválidos", "Los valores deben ser mayores a 0", NULL, ALLEGRO_MESSAGEBOX_ERROR);
								return;
							}
							ejecutando = true;
							al_start_timer(getTimer());
						}
						else if (b.texto == "First Fit") {
							firstBest = false;
						}
						else if (b.texto == "Best Fit") {
							firstBest = true;
						}
					}
				}
			}
			else if (ev.type == ALLEGRO_EVENT_KEY_CHAR) {
				for (auto& campo : camposForm) {
					if (campo.activo) {
						if (ev.keyboard.keycode == ALLEGRO_KEY_BACKSPACE && !campo.contenido.empty()) {
							campo.contenido.pop_back();
						}
						else if (ev.keyboard.unichar >= '0' && ev.keyboard.unichar <= '9' && campo.contenido.size() <= 4) { // Printable characters
							campo.contenido += static_cast<char>(ev.keyboard.unichar);
						}
					}
				}
			}
		}
		else if (ev.type == ALLEGRO_EVENT_TIMER) {
			if (contAux++ >= 2) {
				contAux = 0;
				int id = rand() % 1000 + 1;
				int tamano = rand() % tamanoMax + 100;
				int quantum = rand() % quantumProceso + 1;
				if (firstBest)
					bestFit(id, tamano, quantum);
				else
					firstFit(id, tamano, quantum);
				}
				turnoProceso();
		}
		else if (ev.type == ALLEGRO_EVENT_MOUSE_BUTTON_DOWN) {
			float x = ev.mouse.x;
			float y = ev.mouse.y;
			for (auto& b : botonesEjecucion) {
				if (b.contiene(x, y)) {
					if (b.texto == "Pausar") {
						al_stop_timer(getTimer());
					}
					else if (b.texto == "Reanudar") {
						al_start_timer(getTimer());
					}
					else if (b.texto == "- Velocidad") {
						if (velocidadActual > 0)
							velocidadActual--;
						al_set_timer_speed(getTimer(), getVelocidad());
					}
					else if (b.texto == "+ Velocidad") {
						if (velocidadActual < 4)
							velocidadActual++;
						al_set_timer_speed(getTimer(), getVelocidad());
					}
					else if (b.texto == "Terminar") {
						ejecutando = false;
						al_stop_timer(getTimer());
						memoria.clear();
						memoria.push_back({ 0, 8192, 0 });
						roundRobin.clear();
						al_show_native_message_box(display, "Simulación terminada", "Estadisticas: ", ("Procesos atendidos: " + to_string(procesosAtendidos) + "\nProcesos sin entrar: " + to_string(proSinEntrar) + "\nMemoria ocupada promedio: " + to_string((int)(memOcupPor / (proTotal + 1))) + "%").c_str(), NULL, 0);
						procesosAtendidos = 0;
						proSinEntrar = 0;
						memOcupada = 0;
						proTotal = 0;
						memOcupPor = 0;
						for (auto& campo : camposForm) {
							campo.contenido.clear();
							campo.activo = false;
						}
					}
				}
			}
		}
	}
	void dibujar(ALLEGRO_FONT* fuente) override {
		ALLEGRO_FONT* fuenteTit = al_load_ttf_font("balatro.ttf", 40, 0);
		al_draw_text(fuenteTit, al_map_rgb(0, 0, 0), 300, 50, ALLEGRO_ALIGN_LEFT, "Planificador Hello Kitty");
		al_destroy_font(fuenteTit);


		if (!ejecutando) {
			for (auto& campo : camposForm) {
				al_draw_filled_rectangle(campo.x2, campo.y1, campo.x3, campo.y2, al_map_rgb(255, 255, 255));
				al_draw_rectangle(campo.x2, campo.y1, campo.x3, campo.y2, al_map_rgb(0, 0, 0), 2);
				al_draw_text(fuente, al_map_rgb(0, 0, 0), campo.x1, campo.y1+10, ALLEGRO_ALIGN_LEFT, campo.texto.c_str());
				al_draw_text(fuente, al_map_rgb(0, 0, 0), (campo.x2 + campo.x3) / 2, campo.y1+10, ALLEGRO_ALIGN_CENTER, campo.contenido.c_str());
			}
			for (auto& b : botonesForm) {
				al_draw_filled_rectangle(b.x1, b.y1, b.x2, b.y2, al_map_rgb(0, 0, 0));
				al_draw_text(fuente, al_map_rgb(255, 255, 255), (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2 - 10, ALLEGRO_ALIGN_CENTER, b.texto.c_str());
			}
			if(!firstBest) 
				al_draw_rectangle(botonesForm[0].x1, botonesForm[0].y1, botonesForm[0].x2, botonesForm[0].y2, al_map_rgb(204, 204, 0), 5);
			else 
				al_draw_rectangle(botonesForm[1].x1, botonesForm[1].y1, botonesForm[1].x2, botonesForm[1].y2, al_map_rgb(204, 204, 0), 5);
		}
		else {
			al_draw_rectangle(recuadroMemoria.x1, recuadroMemoria.y1, recuadroMemoria.x2, recuadroMemoria.y2, al_map_rgb(0, 0, 0), 2);
			al_draw_textf(fuente, al_map_rgb(0,0,0), 420, 160, ALLEGRO_ALIGN_LEFT, "Memoria: %d KB", tamanoMem);
			double espacio = 0;
			double escala = (recuadroMemoria.x2 - recuadroMemoria.x1) / (double)tamanoMem;
			int suma = 0;
			for (auto& p : memoria) {
				if(p.ID == 0) {
					al_draw_filled_rectangle(recuadroMemoria.x1 + espacio, recuadroMemoria.y1, recuadroMemoria.x1 + espacio + p.tamano * escala, recuadroMemoria.y2, al_map_rgb(255, 255, 255));
					al_draw_rectangle(recuadroMemoria.x1 + espacio, recuadroMemoria.y1, recuadroMemoria.x1 + espacio + p.tamano * escala, recuadroMemoria.y2, al_map_rgb(0, 0, 0), 2);
					if (p.tamano * escala > 30)
						al_draw_text(fuente, al_map_rgb(0,0,0), recuadroMemoria.x1 + espacio + (p.tamano * escala) / 2, recuadroMemoria.y1 + 10, ALLEGRO_ALIGN_CENTER, "Hueco");
				}
				else {
					al_draw_filled_rectangle(recuadroMemoria.x1 + espacio, recuadroMemoria.y1, recuadroMemoria.x1 + espacio + p.tamano * escala, recuadroMemoria.y2, al_map_rgb(0, 204, 0));
					al_draw_rectangle(recuadroMemoria.x1 + espacio, recuadroMemoria.y1, recuadroMemoria.x1 + espacio + p.tamano * escala, recuadroMemoria.y2, al_map_rgb(0, 0, 0), 2);
					if (p.tamano * escala > 30)
						al_draw_text(fuente, al_map_rgb(0,0,0), recuadroMemoria.x1 + espacio + (p.tamano * escala) / 2, recuadroMemoria.y1 + 10, ALLEGRO_ALIGN_CENTER, ("" + to_string(p.ID)).c_str());
					suma += p.tamano;
				}
				espacio += p.tamano * escala;


			}
			memOcupada = (double)suma*100 / tamanoMem;
			memOcupPor += memOcupada;
			proTotal++;
			for (auto& b : botonesEjecucion) {
				al_draw_filled_rectangle(b.x1, b.y1, b.x2, b.y2, al_map_rgb(0, 0, 0));
				al_draw_text(fuente, al_map_rgb(255, 255, 255), (b.x1 + b.x2) / 2, (b.y1 + b.y2) / 2 - 10, ALLEGRO_ALIGN_CENTER, b.texto.c_str());
			}
			espacio = 0;
			al_draw_rectangle(600, 400, 800, 450, al_map_rgb(0, 0, 0), 2);

			if (!roundRobin.empty()) 
				al_draw_text(fuente, al_map_rgb(0, 0, 0), 700, 405, ALLEGRO_ALIGN_CENTER, ("En ejecucion: " + to_string(roundRobin.front().ID)).c_str());
			
			al_draw_text(fuente, al_map_rgb(0, 0, 0), 350, 220, ALLEGRO_ALIGN_CENTER, "Cola de listos");
			for (auto& r : roundRobin) {
				if (r.ID == roundRobin.front().ID)
					continue;
				al_draw_rectangle(100, 250 + espacio, 300, 320 + espacio, al_map_rgb(0, 0, 0), 2);
				al_draw_text(fuente, al_map_rgb(0,0,0), 200, 255 + espacio, ALLEGRO_ALIGN_CENTER, ("ID: " + to_string(r.ID) + " QRest: " + to_string(r.quantum)).c_str());
				al_draw_text(fuente, al_map_rgb(0, 0, 0), 200, 285 + espacio, ALLEGRO_ALIGN_CENTER, ("Tam: " + to_string(r.tamano)).c_str());
				espacio += 70;
			}

			/* Stats */
			al_draw_text(fuente, al_map_rgb(0, 0, 0), 1000, 460, ALLEGRO_ALIGN_LEFT, ("Procesos atendidos: " + to_string(procesosAtendidos)).c_str());
			al_draw_text(fuente, al_map_rgb(0, 0, 0), 1000, 490, ALLEGRO_ALIGN_LEFT, ("Procesos sin entrar: " + to_string(proSinEntrar)).c_str());
			al_draw_text(fuente, al_map_rgb(0, 0, 0), 1000, 520, ALLEGRO_ALIGN_LEFT, ("Memoria ocupada: " + to_string((int)memOcupada) + "%").c_str());
		}
	}
};


int main() {
	al_init();
	al_install_mouse();
	al_install_keyboard();
	al_init_font_addon();
	al_init_ttf_addon();
	al_init_native_dialog_addon();

	srand(time(NULL));

	ALLEGRO_MONITOR_INFO pantalla;
	al_get_monitor_info(0, &pantalla);
	int anchoP = pantalla.x2 - pantalla.x1;
	int altoP = pantalla.y2 - pantalla.y1;

	Pantalla* pantallaActual = new PantallaUni(al_create_display(anchoP, altoP - 75), al_create_event_queue());
	ALLEGRO_FONT* fuente = al_load_ttf_font("balatro.ttf", 20, 0);

	bool salir = false;

	ALLEGRO_EVENT ev;
	al_register_event_source(pantallaActual->getEventQueue(), al_get_timer_event_source(pantallaActual->getTimer()));
	al_register_event_source(pantallaActual->getEventQueue(), al_get_display_event_source(pantallaActual->getDisplay()));
	al_register_event_source(pantallaActual->getEventQueue(), al_get_mouse_event_source());
	al_register_event_source(pantallaActual->getEventQueue(), al_get_keyboard_event_source());

	while (!salir) {
			al_clear_to_color(al_map_rgb(224, 224, 224));
			pantallaActual->dibujar(fuente);
			al_flip_display();

			al_wait_for_event(pantallaActual->getEventQueue(), &ev);
			if (ev.type == ALLEGRO_EVENT_DISPLAY_CLOSE) {
				salir = true;
			}
			else {
				pantallaActual->evento(ev, pantallaActual);
			}
	}

	ALLEGRO_DISPLAY* d = pantallaActual->getDisplay();
	ALLEGRO_EVENT_QUEUE* q = pantallaActual->getEventQueue();
	ALLEGRO_TIMER* t = pantallaActual->getTimer();
	delete pantallaActual;
	al_destroy_font(fuente);
	al_destroy_event_queue(q);
	al_destroy_display(d);
	al_destroy_timer(t);
	
	return 0;
}