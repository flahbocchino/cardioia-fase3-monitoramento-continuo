CardioIA — Fase 3: Monitoramento Contínuo — IoT na Saúde
Relatório Parte 1 — Armazenamento e Processamento Local (Edge Computing)
Flávia Nunes Bocchino | RM 564213 | FIAP

1. Visão geral do sistema
Esta primeira parte da Fase 3 tem como objetivo demonstrar o papel do Edge Computing em um cenário de monitoramento cardíaco contínuo. O sistema desenvolvido simula um dispositivo vestível que captura dois sinais vitais — temperatura corporal e frequência cardíaca — e implementa uma lógica de resiliência offline capaz de armazenar leituras localmente quando a conectividade com a nuvem está indisponível, garantindo que nenhum dado seja perdido durante períodos de desconexão.
A implementação foi realizada no simulador Wokwi, utilizando um microcontrolador ESP32. Dois sensores distintos foram integrados ao circuito: o DHT22, sensor de temperatura e umidade, e um botão de pressão que simula a contagem de batimentos cardíacos. O código foi desenvolvido em C++ (Arduino Framework) com comentários didáticos ao longo de toda a lógica.

2. Componentes e função de cada um
O DHT22 é o sensor responsável pela leitura de temperatura e umidade relativa do ar. No contexto do CardioIA, a temperatura representa um dado clínico relevante: elevações persistentes acima de 38°C são indicativo de estado febril, que pode agravar condições cardiovasculares preexistentes. O sensor é lido a cada 5 segundos e os valores são validados internamente (valores NaN gerados por falha de leitura são substituídos por -1 para não contaminar o dataset).
O botão de pressão simula a captação de batimentos cardíacos. O usuário pressiona o botão repetidamente durante uma janela de 10 segundos; ao final dessa janela, o sistema multiplica o total de pressões por 6 para extrapolar a frequência por minuto (BPM). Esta é uma simplificação deliberada e adequada ao contexto do simulador, onde sensores cardíacos físicos como o MAX30100 não estão disponíveis. Em um dispositivo real, este sensor seria substituído por um fotopletismógrafo ou eletrodo ECG. Um debounce de 50 milissegundos foi implementado para evitar contagens duplas por bouncing mecânico do botão.

3. Fluxo de funcionamento
O loop principal do sistema opera sobre quatro responsabilidades simultâneas, gerenciadas por temporização não bloqueante (sem uso de delay() prolongado, o que travaria a leitura do botão):
a) Leitura do botão: verificada a cada iteração do loop, com debounce, acumulando a contagem de pressões dentro da janela de 10 segundos.
b) Cálculo de BPM: ao fim de cada janela de 10 segundos, o total de pressões é convertido em BPM e o contador é zerado para a próxima janela.
c) Leitura e armazenamento: a cada 5 segundos, uma leitura completa (temperatura, umidade, BPM e timestamp em milissegundos) é gerada. Dependendo do estado de conectividade, ela é enviada imediatamente para a nuvem ou enfileirada localmente.
d) Toggle de conectividade: a variável wifi_conectado alterna automaticamente a cada 30 segundos, simulando quedas e retomadas de rede. Ao reconectar, a função de sincronização é chamada imediatamente.

4. Estratégia de resiliência offline
A resiliência offline é o núcleo desta entrega. Como o Wokwi não oferece suporte ao SPIFFS de forma persistente — o sistema de arquivos é volátil e não sobrevive ao encerramento da simulação — a estratégia adotada foi o armazenamento em fila circular (ring buffer) em RAM.
Esta escolha é tecnicamente coerente com o contexto de Edge Computing: o ESP32 possui 520 KB de RAM interna, o que suporta confortavelmente centenas de structs do tipo Leitura (cada uma ocupa menos de 20 bytes). A fila foi dimensionada para até 50 leituras, o que, com um intervalo de coleta de 5 segundos, cobre aproximadamente 4 minutos de operação offline antes que dados mais antigos comecem a ser descartados.
Esta decisão de design reflete uma escolha de negócio real: em um wearable cardíaco de uso clínico, 4 minutos de buffer offline é suficiente para cobrir a maioria das quedas transitórias de rede sem risco de perda relevante de dados. Dispositivos com requisitos maiores de desconexão exigiriam armazenamento em flash (SPIFFS real ou cartão microSD), o que seria a evolução natural desta implementação para hardware físico.
O comportamento da fila é o seguinte: enquanto offline, cada nova leitura é inserida no fim da fila (fila_push). Quando a fila atinge 50 registros, o mais antigo é automaticamente descartado, e um aviso é emitido no Monitor Serial. Ao reconectar, a função sincronizar_fila descarrega todos os registros acumulados na ordem em que foram coletados (FIFO), enviando-os um a um para a "nuvem" — representada, nesta fase, pelo próprio Monitor Serial. Em produção, esta função será substituída pelo envio via MQTT, já preparado arquiteturalmente para a Parte 2.

5. Saída no Monitor Serial
O Monitor Serial cumpre dois papéis nesta etapa: é a interface de depuração local e, simultaneamente, o substituto funcional do SPIFFS para fins de registro. Toda leitura é impressa com prefixo [OFFLINE] ou [ONLINE], permitindo rastrear claramente os estados do sistema. Ao reconectar, a saída [SYNC] documenta a sincronização dos dados acumulados, incluindo o número de registros processados.

6. Preparação para a Parte 2
A arquitetura do código foi deliberadamente estruturada para facilitar a transição para a Parte 2. A função enviar_para_nuvem() está isolada e claramente identificada como ponto de substituição: na Parte 2, ela será reimplementada para publicar a leitura via MQTT em um broker HiveMQ Cloud, mantendo toda a lógica de fila, leitura de sensores e resiliência offline intacta. Esta separação de responsabilidades é uma boa prática de engenharia de software aplicada ao desenvolvimento embarcado.

7. Conclusão
O sistema implementado demonstra, em ambiente simulado, o ciclo completo de captura e armazenamento local de dados vitais com resiliência a falhas de conectividade. A lógica de Edge Computing garante que o dispositivo continue operando e coletando dados mesmo sem acesso à rede, sincronizando o histórico acumulado no momento em que a conexão é restabelecida. Esta capacidade é essencial em aplicações médicas reais, onde a perda de dados durante uma arritmia ou evento cardíaco transitório pode ter consequências clínicas diretas.
