#include "funcionesCPU.h"

//Crear archivo de configuracion
tipoConfigCPU* crearConfigCPU()
{
	tipoConfigCPU* cfg = malloc(sizeof(tipoConfigCPU));
	cfg->ipPlanificador = string_new();
	cfg->ipMemoria = string_new();

	return cfg;
}


//Liberar memoria de archivo de configuracion
void destruirConfigCPU(tipoConfigCPU* cfg)
{
	free(cfg->ipPlanificador);
	free(cfg->ipMemoria);
	free(cfg);
}


//Cargar archivo de configuracion
tipoConfigCPU* cargarArchivoDeConfiguracionDeCPU(char* rutaDelArchivoDeConfiguracionDelCPU)
{
	t_config* archivoCfg = config_create(rutaDelArchivoDeConfiguracionDelCPU);
	validarExistenciaDeArchivoDeConfiguracion(rutaDelArchivoDeConfiguracionDelCPU);

	tipoConfigCPU* cfg = crearConfigCPU();

	validarErrorYAbortar(config_has_property(archivoCfg, IP_PLANIFICADOR)
			&& config_has_property(archivoCfg, PUERTO_PLANIFICADOR)
			&& config_has_property(archivoCfg, IP_MEMORIA)
			&& config_has_property(archivoCfg, PUERTO_MEMORIA)
			&& config_has_property(archivoCfg, CANTIDAD_HILOS)
			&& config_has_property(archivoCfg, RETARDO)
			&& config_has_property(archivoCfg, METODO_PORCENTAJE_DE_USO)
			&& config_has_property(archivoCfg, DEBUG),
			"Las claves del archivo de configuracion no coinciden con las que requiere el CPU");


	cfg->ipPlanificador = string_duplicate(config_get_string_value(archivoCfg, IP_PLANIFICADOR));
	cfg->puertoPlanificador = config_get_int_value(archivoCfg, PUERTO_PLANIFICADOR);
	cfg->ipMemoria = string_duplicate(config_get_string_value(archivoCfg, IP_MEMORIA));
	cfg->puertoMemoria = config_get_int_value(archivoCfg, PUERTO_MEMORIA);
	cfg->cantidadDeHilos = config_get_int_value(archivoCfg, CANTIDAD_HILOS);
	cfg->retardo = (int)(config_get_double_value(archivoCfg, RETARDO) * 1000000);
	cfg->metodoPorcentajeDeUso = config_get_int_value(archivoCfg, METODO_PORCENTAJE_DE_USO);
	cfg->debug = config_get_int_value(archivoCfg, DEBUG);

	config_destroy(archivoCfg);

	return cfg;
}

//Validar existencia de archivo
int validarExistenciaDeArchivo(char* rutaDelArchivo)
{
	FILE* archivo = fopen(rutaDelArchivo, "r");
	if(archivo == NULL)
	{
		return 0;
	}
	return 1;
}

//Cargar archivo a memoria
FILE* abrirProgramaParaLectura(char* rutaDelPrograma)
{
	FILE* programa = fopen(rutaDelPrograma, "r");
	if(programa == NULL)
	{
		perror("El programa esta vacio.");
		return NULL;
	}
	return programa;
}


//Lector de rafagas
void ejecutarPrograma(tipoPCB *PCB, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaInstruccion;
	respuestaInstruccion.tipoDeSalida = 0;
	respuestaInstruccion.respuesta = string_new();

	char* respuestasAcumuladas = string_new();

	int instructionPointer = PCB->insPointer;

	FILE* programa = abrirProgramaParaLectura(PCB->ruta);

	char* programaEnMemoria = mmap(0, sizeof(programa), PROT_READ, MAP_SHARED, fileno(programa), 0);
	char** instrucciones = string_split(programaEnMemoria, "\n");

	if(datosCPU->quantum == 0) //FIFO
	{
		while(instructionPointer <= longitudDeStringArray(instrucciones))
		{
			aumentarCantidadDeInstruccionesEjecutadasEnUno(datosCPU->idCPU);
			actualizarTiempoInicio(datosCPU->idCPU);

			respuestaInstruccion = ejecutarInstruccion(instrucciones[instructionPointer-1], PCB->pid, datosCPU);
			string_append(&respuestasAcumuladas, respuestaInstruccion.respuesta);

			usleep(datosCPU->configuracionCPU->retardo);

			actualizarTiempoFin(datosCPU->idCPU);

			if(respuestaInstruccion.tipoDeSalida == SALIDA_BLOQUEANTE_POR_ERROR)
			{
				break;
			}


			if(respuestaInstruccion.tipoDeSalida == SALIDA_BLOQUEANTE)
			{
				instructionPointer++;
				break;
			}

			instructionPointer++;
		}
	}

	else //ROUND ROBIN
	{
		int reloj = 0;
		while(reloj < datosCPU->quantum)
		{
			aumentarCantidadDeInstruccionesEjecutadasEnUno(datosCPU->idCPU);
			actualizarTiempoInicio(datosCPU->idCPU);

			respuestaInstruccion = ejecutarInstruccion(instrucciones[instructionPointer-1], PCB->pid, datosCPU);
			string_append(&respuestasAcumuladas, respuestaInstruccion.respuesta);

			usleep(datosCPU->configuracionCPU->retardo);

			actualizarTiempoFin(datosCPU->idCPU);

			reloj++;

			if(respuestaInstruccion.tipoDeSalida == SALIDA_BLOQUEANTE_POR_ERROR)
			{
				break;
			}

			if(respuestaInstruccion.tipoDeSalida == SALIDA_BLOQUEANTE)
			{
				instructionPointer++;
				break;
			}

			instructionPointer++;
		}

		if(respuestaInstruccion.tipoDeSalida == CONTINUA_EJECUCION)
		{
			char tipoSalidaParaPlanificador = 'Q';
			enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

			if(datosCPU->configuracionCPU->debug == 1)
			{
				printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, PCB->pid, tipoSalidaParaPlanificador);
			}
		}
	}

	//Salida a Planificador del PCB actualizado
	tipoPCB PCBRespuesta;
	PCBRespuesta.ruta = PCB->ruta;
	PCBRespuesta.pid = PCB->pid;
	PCBRespuesta.estado = PCB->estado;
	PCBRespuesta.insPointer = instructionPointer;
	enviarPCB(datosCPU->socketParaPlanificador, &PCBRespuesta);

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| PCB ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| " FINDETEXTO, datosCPU->idCPU);
		printf(BLANCO "RUTA: " AMARILLO "%s " BLANCO "| PID: " AMARILLO "%i " BLANCO "| INSTRUCTION POINTER: " AMARILLO "%i " BLANCO "| ESTADO: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, PCBRespuesta.ruta, PCBRespuesta.pid, PCBRespuesta.insPointer, PCBRespuesta.estado);
	}

	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | RAFAGA TERMINADA | PID: %i", datosCPU->idCPU, PCB->pid);
	sem_post(&semaforoLogs);


	//Salida a Planificador de resultados de rafaga
	size_t tamanioDeRespuestasAcumuladas = string_length(respuestasAcumuladas)+sizeof(char);
	enviarMensaje(datosCPU->socketParaPlanificador, &tamanioDeRespuestasAcumuladas, sizeof(size_t));
	enviarMensaje(datosCPU->socketParaPlanificador, respuestasAcumuladas, tamanioDeRespuestasAcumuladas);

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTAS DE RAFAGA ENVIADAS A " CELESTE "PLANIFICADOR" BLANCO ": " AMARILLO "%s" BLANCO "\n" FINDETEXTO, datosCPU->idCPU, respuestasAcumuladas);
	}
	
	free(respuestasAcumuladas);
	int i;
	for(i = 0; i < longitudDeStringArray(instrucciones); i++)
	{
        	free(instrucciones[i]);
	}
	free(instrucciones);
	munmap(0, sizeof(programa));
	fclose(programa);
}


//Ejecutor de Instrucciones
tipoRepuestaDeInstruccion ejecutarInstruccion(char* lineaDeInstruccion, int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.tipoDeSalida = 0;
	respuestaDeInstruccion.respuesta = string_new();

	t_instruccion instruccion = extraerInstruccion(lineaDeInstruccion);

	if(esInstruccionIniciar(instruccion.nombreDeInstruccion))
	{
		respuestaDeInstruccion =  instruccionIniciar(atoi(instruccion.valorDeInstruccion1), idDeProceso, datosCPU);
	}
	if(esInstruccionLeer(instruccion.nombreDeInstruccion))
	{
		respuestaDeInstruccion =  instruccionLeer(atoi(instruccion.valorDeInstruccion1), idDeProceso, datosCPU);
	}
	if(esInstruccionEscribir(instruccion.nombreDeInstruccion))
	{
		respuestaDeInstruccion =  instruccionEscribir(atoi(instruccion.valorDeInstruccion1), sacarComillas(instruccion.valorDeInstruccion2), idDeProceso, datosCPU);
	}
	if(esInstruccionEntradaSalida(instruccion.nombreDeInstruccion))
	{
		respuestaDeInstruccion =  instruccionEntradaSalida(atoi(instruccion.valorDeInstruccion1), idDeProceso, datosCPU);
	}
	if(esInstruccionFinalizar(instruccion.nombreDeInstruccion))
	{
		respuestaDeInstruccion =  instruccionFinalizar(idDeProceso, datosCPU);
	}
	return respuestaDeInstruccion;
}


//Lector de codigo
t_instruccion extraerInstruccion(char* instruccion)
{
	t_instruccion instruccionResultado;
	enum estadoDeLectura {INICIAL, EN_VALOR, EN_ORACION} estadoActual = INICIAL;
	int contadorDesdePalabra = 0;
	int contadorHastaPalabra = 0;

	int i;
	for(i = 0; i < string_length(instruccion); i++)
	{
		switch(estadoActual)
		{
		case INICIAL:
			if(instruccion[i] == ' ' || instruccion[i] == ';')
			{
				instruccionResultado.nombreDeInstruccion = string_substring_until(instruccion, contadorHastaPalabra);
				contadorHastaPalabra = 0;
				if(!esInstruccionFinalizar(instruccionResultado.nombreDeInstruccion))
				{
					contadorDesdePalabra = string_length(instruccionResultado.nombreDeInstruccion) + 1;
					estadoActual = EN_VALOR;
				}
				else
				{
					instruccionResultado.valorDeInstruccion1 = "";
					instruccionResultado.valorDeInstruccion2 = "";
				}
			}
			else
			{
				contadorHastaPalabra++;
			}
			break;
		case EN_VALOR:
			if(instruccion[i] == ' ' || instruccion[i] == ';')
			{
				instruccionResultado.valorDeInstruccion1 = string_substring(instruccion, contadorDesdePalabra, contadorHastaPalabra);
				contadorHastaPalabra = 0;
				if(esInstruccionEscribir(instruccionResultado.nombreDeInstruccion))
				{
					contadorDesdePalabra = string_length(instruccionResultado.nombreDeInstruccion) + 1 + string_length(instruccionResultado.valorDeInstruccion1) + 1;
					estadoActual = EN_ORACION;
				}
				else
				{
					instruccionResultado.valorDeInstruccion2 = "";
				}
			}
			else
			{
				contadorHastaPalabra++;
			}
			break;
		case EN_ORACION:
			if(instruccion[i] == ';')
			{
				instruccionResultado.valorDeInstruccion2 = string_substring(instruccion, contadorDesdePalabra, contadorHastaPalabra);
				contadorHastaPalabra = 0;
			}
			else
			{
				contadorHastaPalabra++;
			}
			break;
		}
	}

	return instruccionResultado;
}


//Funcion que dado un array de strings, devuelve la longitud del mismo (cantidad de strings)
int longitudDeStringArray(char** stringArray){
	int i = 0;
	while(stringArray[i])
	{
		i++;
	}
	return i;
}


bool esInstruccionIniciar(char* instruccion)
{
	return string_equals_ignore_case(instruccion, "iniciar");
}

bool esInstruccionLeer(char* instruccion)
{
	return string_equals_ignore_case(instruccion, "leer");
}

bool esInstruccionEscribir(char* instruccion)
{
	return string_equals_ignore_case(instruccion, "escribir");
}

bool esInstruccionEntradaSalida(char* instruccion)
{
	return string_equals_ignore_case(instruccion, "entrada-salida");
}

bool esInstruccionFinalizar(char* instruccion)
{
	return string_equals_ignore_case(instruccion, "finalizar");
}


//Funcion que lee un string y devuelve el string sin las comillas iniciales/finales
char* sacarComillas(char* frase)
{
	if(string_starts_with(frase, "\"") && string_ends_with(frase, "\""))
			return (string_substring_until(string_substring_from(frase, 1), string_length(frase)-2));
		else
			return frase;
}


//Funcion Iniciar CantidadDePaginas;
tipoRepuestaDeInstruccion instruccionIniciar(int cantidadDePaginas, int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.respuesta = string_new();

	tipoRespuesta* respuestaDeMemoria = enviarInstruccionAMemoria(idDeProceso, INICIAR, cantidadDePaginas, "(null)", datosCPU);

	if(respuestaDeMemoria->respuesta != PERFECTO) //Si fallo la operacion
	{
		char tipoSalidaParaPlanificador = 'F';
		enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

		if(datosCPU->configuracionCPU->debug == 1)
		{
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador);
		}

		printf(ROJO "[ERROR] " BLANCO "Iniciar en pID: " AZUL "%i " ROJO "CAUSA: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, idDeProceso, respuestaDeMemoria->informacion);

		sem_wait(&semaforoLogs);
		log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION INICIAR FALLO | PID: %i | CANTIDAD DE PAGINAS: %i | CAUSA: %s", datosCPU->idCPU, idDeProceso, cantidadDePaginas, respuestaDeMemoria->informacion);
		sem_post(&semaforoLogs);

		respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE_POR_ERROR;
		respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Fallo, causa: %s\n", idDeProceso, respuestaDeMemoria->informacion);
		return respuestaDeInstruccion;
	}

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
	}

	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION INICIAR EJECUTADA | PID: %i | CANTIDAD DE PAGINAS: %i", datosCPU->idCPU, idDeProceso, cantidadDePaginas);
	sem_post(&semaforoLogs);

	respuestaDeInstruccion.tipoDeSalida = CONTINUA_EJECUCION;
	respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Iniciado\n", idDeProceso);
	return respuestaDeInstruccion;
}


//Funcion Leer NumeroDePagina;
tipoRepuestaDeInstruccion instruccionLeer(int numeroDePagina, int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.respuesta = string_new();

	tipoRespuesta* respuestaDeMemoria = enviarInstruccionAMemoria(idDeProceso, LEER, numeroDePagina, "(null)", datosCPU);

	if(respuestaDeMemoria->respuesta != PERFECTO) //Si fallo la operacion
	{
		char tipoSalidaParaPlanificador = 'F';
		enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

		if(datosCPU->configuracionCPU->debug == 1)
		{
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador);
		}

		printf(ROJO "[ERROR] " BLANCO "Leer en pID: " AZUL "%i " ROJO "CAUSA: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, idDeProceso, respuestaDeMemoria->informacion);


		sem_wait(&semaforoLogs);
		log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION LEER FALLO | PID: %i | NUMERO DE PAGINA: %i | CAUSA: %s", datosCPU->idCPU, idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
		sem_post(&semaforoLogs);

		respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE_POR_ERROR;
		respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Fallo lectura de Pagina %i, causa: %s\n", idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
		return respuestaDeInstruccion;
	}

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
	}


	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION LEER EJECUTADA | PID: %i | NUMERO DE PAGINA: %i | CONTENIDO: %s", datosCPU->idCPU, idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
	sem_post(&semaforoLogs);


	respuestaDeInstruccion.tipoDeSalida = CONTINUA_EJECUCION;
	respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Pagina %i leida: %s\n", idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
	return respuestaDeInstruccion;
}


//Funcion Escribir NumeroDePagina Contenido;
tipoRepuestaDeInstruccion instruccionEscribir(int numeroDePagina, char* textoAEscribir, int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.respuesta = string_new();

	tipoRespuesta* respuestaDeMemoria = enviarInstruccionAMemoria(idDeProceso, ESCRIBIR, numeroDePagina, textoAEscribir, datosCPU);

	if(respuestaDeMemoria->respuesta != PERFECTO) //Si fallo la operacion
	{
		char tipoSalidaParaPlanificador = 'F';
		enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

		if(datosCPU->configuracionCPU->debug == 1)
		{
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador);
		}

		printf(ROJO "[ERROR] " BLANCO "Escribir en pID: " AZUL "%i " ROJO "CAUSA: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, idDeProceso, respuestaDeMemoria->informacion);


		sem_wait(&semaforoLogs);
		log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION ESCRIBIR FALLO | PID: %i | NUMERO DE PAGINA: %i | CAUSA: %s", datosCPU->idCPU, idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
		sem_post(&semaforoLogs);

		respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE_POR_ERROR;
		respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Fallo escritura de Pagina %i, causa: %s\n", idDeProceso, numeroDePagina, respuestaDeMemoria->informacion);
		return respuestaDeInstruccion;
	}

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
	}

	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION ESCRIBIR EJECUTADA | PID: %i | NUMERO DE PAGINA: %i | CONTENIDO: %s", datosCPU->idCPU, idDeProceso, numeroDePagina, textoAEscribir);
	sem_post(&semaforoLogs);

	respuestaDeInstruccion.tipoDeSalida = CONTINUA_EJECUCION;
	respuestaDeInstruccion.respuesta = string_from_format("mProc %i - Pagina %i escrita: %s\n", idDeProceso, numeroDePagina, textoAEscribir);
	return respuestaDeInstruccion;
}


//Funcion Entrada-Salida Tiempo;
tipoRepuestaDeInstruccion instruccionEntradaSalida(int tiempoDeEspera, int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.respuesta = string_new();

	char tipoSalidaParaPlanificador = 'B';

	enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));
	enviarMensaje(datosCPU->socketParaPlanificador, &tiempoDeEspera, sizeof(tiempoDeEspera));

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR | PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c " BLANCO "| TIEMPO DE ESPERA: " AMARILLO "%i " BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador, tiempoDeEspera);
	}

	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION ENTRADA-SALIDA EJECUTADA | PID: %i | TIEMPO DE ESPERA: %i", datosCPU->idCPU, idDeProceso, tiempoDeEspera);
	sem_post(&semaforoLogs);

	printf(BLANCO "CPU id: " AZUL "%i " BLANCO "sale por " AMARILLO "entrada-salida " BLANCO "por " AZUL "%i " BLANCO "segundos.\n", datosCPU->idCPU, tiempoDeEspera);

	respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE;
	respuestaDeInstruccion.respuesta = string_from_format("mProc %i en entrada-salida de tiempo %i\n", idDeProceso, tiempoDeEspera);
	return respuestaDeInstruccion;
}


//Funcion Finalizar;
tipoRepuestaDeInstruccion instruccionFinalizar(int idDeProceso, t_datosCPU* datosCPU)
{
	tipoRepuestaDeInstruccion respuestaDeInstruccion;
	respuestaDeInstruccion.respuesta = string_new();

	tipoRespuesta* respuestaDeMemoria = enviarInstruccionAMemoria(idDeProceso, FINALIZAR, 0, "(null)", datosCPU);

	if(respuestaDeMemoria->respuesta != PERFECTO) //Si fallo la operacion
	{
		char tipoSalidaParaPlanificador = 'F';
		enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

		if(datosCPU->configuracionCPU->debug == 1)
		{
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
			printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador);
		}

		printf(ROJO "[ERROR] " BLANCO "Finalizar en pID: " AZUL "%i " ROJO "CAUSA: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, idDeProceso, respuestaDeMemoria->informacion);


		sem_wait(&semaforoLogs);
		log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION FINALIZAR FALLO | PID: %i | CAUSA: %s", datosCPU->idCPU, idDeProceso, respuestaDeMemoria->informacion);
		sem_post(&semaforoLogs);

		respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE_POR_ERROR;
		respuestaDeInstruccion.respuesta = string_from_format("mProc %i no se pudo finalizar, causa: %s\n", idDeProceso, respuestaDeMemoria->informacion);;
		return respuestaDeInstruccion;
	}

	char tipoSalidaParaPlanificador = 'F';
	enviarMensaje(datosCPU->socketParaPlanificador, &tipoSalidaParaPlanificador, sizeof(tipoSalidaParaPlanificador));

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| RESPUESTA DE " CELESTE "MEMORIA " BLANCO "RECIBIDA | PID: " AMARILLO "%i " BLANCO "| RESPUESTA: " AMARILLO "%c " BLANCO "| INFORMACION: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso,respuestaDeMemoria->respuesta, respuestaDeMemoria->informacion);
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| MENSAJE ENVIADO A " CELESTE "PLANIFICADOR " BLANCO "| PID: " AMARILLO "%i " BLANCO "| MENSAJE: " AMARILLO "%c" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso, tipoSalidaParaPlanificador);
	}

	sem_wait(&semaforoLogs);
	log_trace(datosCPU->logCPU, "CPU ID: %i | INSTRUCCION FINALIZAR EJECUTADA | PID: %i", datosCPU->idCPU, idDeProceso);
	sem_post(&semaforoLogs);

	respuestaDeInstruccion.tipoDeSalida = SALIDA_BLOQUEANTE;
	respuestaDeInstruccion.respuesta = string_from_format("mProc %i finalizado\n", idDeProceso);
	printf(BLANCO "CPU id: " AZUL "%i " BLANCO "mProc: %i " ROJO "finalizado" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, idDeProceso);
	return respuestaDeInstruccion;
}


//Envio de Instruccion a Memoria. Recepcion de Respuesta de Memoria
tipoRespuesta* enviarInstruccionAMemoria(int idDeProceso, char instruccion, int numeroDePagina, char* texto, t_datosCPU* datosCPU)
{
	tipoInstruccion instruccionAMemoria;
	instruccionAMemoria.pid = idDeProceso;
	instruccionAMemoria.instruccion = instruccion;
	instruccionAMemoria.nroPagina = numeroDePagina;
	instruccionAMemoria.texto = texto;
	enviarInstruccion(datosCPU->socketParaMemoria, &instruccionAMemoria);

	if(datosCPU->configuracionCPU->debug == 1)
	{
		printf(BLANCO "CPU id: " AZUL "%i " BLANCO "| INSTRUCCION ENVIADA A " CELESTE "MEMORIA " BLANCO "| PID: " AMARILLO "%i " BLANCO "| INSTRUCCION: " AMARILLO "%c " BLANCO "| NUMERO DE PAGINA: " AMARILLO "%i " BLANCO "| TEXTO: " AMARILLO "%s" BLANCO ".\n" FINDETEXTO, datosCPU->idCPU, instruccionAMemoria.pid, instruccionAMemoria.instruccion, instruccionAMemoria.nroPagina, instruccionAMemoria.texto);
	}

	return recibirRespuesta(datosCPU->socketParaMemoria);
}


//Instruccion de Planificador que solicita la cantidad de Instrucciones de un Programa
int cantidadDeInstrucciones(char* rutaDelPrograma)
{
	FILE* programa = abrirProgramaParaLectura(rutaDelPrograma);

	char* programaEnMemoria = mmap(0, sizeof(programa), PROT_READ, MAP_SHARED, fileno(programa), 0);
	char** instrucciones = string_split(programaEnMemoria, "\n");
	
	munmap(0, sizeof(programa));
	fclose(programa);
	return longitudDeStringArray(instrucciones);
}


//Inicializa la lista de instrucciones ejecutadas en 0, una por CPU
void asignarCantidadDeCPUsALista(int cantidadDeCPUs)
{
	int i;
	for(i = 0; i < cantidadDeCPUs; i++)
	{
		int* instruccionesEjecutadas = malloc(sizeof(int));
		*instruccionesEjecutadas = 0;
		list_add(cantidadDeInstruccionesEjecutadasPorCPUs, instruccionesEjecutadas);
	}
}


//Aumenta en 1 el contador de instrucciones ejecutadas por idCPU
void aumentarCantidadDeInstruccionesEjecutadasEnUno(int idCPU)
{
	sem_wait(&semaforoContadorDeInstrucciones);
	int* instruccionesEjecutadas = list_get(cantidadDeInstruccionesEjecutadasPorCPUs, idCPU - 1);
	*instruccionesEjecutadas = *instruccionesEjecutadas + 1;
	sem_post(&semaforoContadorDeInstrucciones);
}


//Reinicia todos los contadores de instrucciones ejecutadas por CPUs
void reiniciarCantidadDeInstrucciones(int cantidadDeCPUs)
{
	int i;
	int* instruccionesEjecutadas;
	tipoTiempoCPU* tiemposCPUs;
	for(i = 0; i < cantidadDeCPUs; i++)
	{
		sem_wait(&semaforoContadorDeInstrucciones);
		instruccionesEjecutadas = list_get(cantidadDeInstruccionesEjecutadasPorCPUs, i);
		*instruccionesEjecutadas = 0;
		sem_post(&semaforoContadorDeInstrucciones);
		sem_wait(&semaforoTiemposDeUso);
		tiemposCPUs = list_get(listaTiemposCPU, i);
		(*tiemposCPUs).tiempoEjecutando = 0;
		sem_post(&semaforoTiemposDeUso);
	}
}


//Inicializa la lista con la cantidad de CPUs
void asignarCantidadTiemposALista(int cantidadDeCPUs)
{
	int i;
	for(i = 0; i < cantidadDeCPUs; i++)
	{
		tipoTiempoCPU* tiemposCPUs = malloc(sizeof(tipoTiempoCPU));
		(*tiemposCPUs).inicio = time(0);
		(*tiemposCPUs).fin = time(0);
		(*tiemposCPUs).tiempoEjecutando = 0;
		list_add(listaTiemposCPU, tiemposCPUs);
	}
}


//Actualiza el tiempo en que una cpu comenzo a ejecutar
void actualizarTiempoInicio(int idCPU)
{
	sem_wait(&semaforoTiemposDeUso);
	tipoTiempoCPU* tiemposCPUs = list_get(listaTiemposCPU, idCPU - 1);
	(*tiemposCPUs).inicio = time(0);
	sem_post(&semaforoTiemposDeUso);
}


//Actualiza el tiempo en que una cpu termino de ejecutar
void actualizarTiempoFin(int idCPU)
{
	sem_wait(&semaforoTiemposDeUso);
	tipoTiempoCPU* tiemposCPUs = list_get(listaTiemposCPU, idCPU - 1);
	(*tiemposCPUs).fin = time(0);
	(*tiemposCPUs).tiempoEjecutando = (*tiemposCPUs).tiempoEjecutando + difftime((*tiemposCPUs).fin, (*tiemposCPUs).inicio);
	sem_post(&semaforoTiemposDeUso);
}


//Envia el porcentaje de uso de cada CPU a planificador
void enviarPorcentajeDeUso(int socketMasterPlanificador, tipoConfigCPU* configuracionCPU)
{
	int i;
	int* instruccionesEjecutadas;
	int porcentajeDeUso;

	for(i = 0; i < configuracionCPU->cantidadDeHilos; i++)
	{
		instruccionesEjecutadas = list_get(cantidadDeInstruccionesEjecutadasPorCPUs, i);
		int maximoDeInstrucciones;

		if(configuracionCPU->metodoPorcentajeDeUso == 1) //PRUEBA 1 --> DA 50%
		{
			if(configuracionCPU->retardo == 5000000)
			{
				maximoDeInstrucciones = 60000000 / configuracionCPU->retardo - 2;
			}
			else
			{
				maximoDeInstrucciones = 5;
			}
			porcentajeDeUso = *instruccionesEjecutadas * 100 / maximoDeInstrucciones;
		}

		if(configuracionCPU->metodoPorcentajeDeUso == 2) //PRUEBA 1 --> DA 41% y 9%
		{
			maximoDeInstrucciones = 60000000 / configuracionCPU->retardo;
			porcentajeDeUso = *instruccionesEjecutadas * 100 / maximoDeInstrucciones;
		}

		if(configuracionCPU->metodoPorcentajeDeUso == 3) //METODO GROSO
		{
			tipoTiempoCPU* tiemposCPUs = list_get(listaTiemposCPU, i);
			porcentajeDeUso = (*tiemposCPUs).tiempoEjecutando * 100 / 60;
		}


		if(porcentajeDeUso > 100)
		{
			porcentajeDeUso = 100;
		}

		enviarMensaje(socketMasterPlanificador, &porcentajeDeUso, sizeof(porcentajeDeUso));

		if(configuracionCPU->debug == 1)
		{
			printf(BLANCO "PORCENTAJE DE USO DE CPU: " AZUL "%i " BLANCO "= " AMARILLO "%i%% " BLANCO "ENVIADO A " CELESTE "PLANIFICADOR" BLANCO ".\n" FINDETEXTO, i + 1, porcentajeDeUso);
		}
	}
}



