$(document).ready(function() {
    $('#smooth_transition').change(function() {
        if ($(this).is(':checked')) {
            $('#duration_field').show();
        } else {
            $('#duration_field').hide();
        }
    });

    function strtime_to_minutes(time) {
        const [hours, minutes] = time.split(':').map(Number);
        return hours * 60 + minutes;
    }
    function add_row_to_table(start_time, end_time, smooth_transition, duration, address) {
        const smooth_text = smooth_transition ? 'Да' : 'Нет';
        const duration_text = smooth_transition ? duration : '—';
        function minutes_to_strtime(minutes_raw) {
            const hours = Math.floor(minutes_raw / 60);
            const minutes = minutes_raw % 60;
            return `${hours < 10 ? '0' : ''}${hours}:${minutes < 10 ? '0' : ''}${minutes}`;
        }
        const row = `<tr id="${address}">
                    <td>${minutes_to_strtime(start_time)}</td>
                    <td>${minutes_to_strtime(end_time)}</td>
                    <td>${smooth_text}</td>
                    <td>${duration_text}</td>
                    <td><button class="delete-btn">Удалить</button></td>
                </tr>`;
        $('#record_table tbody').append(row);
    }

    function load_records() {
        $.ajax({
            url: '/get_records',
            method: 'GET',
            success: function(records) {
                $.each(records, function(index, record) {
                    add_row_to_table(record.start_time, record.end_time, record.smooth_transition, record.duration, record.address);
                });
            }
        });
    }


    $('#recordForm').submit(function(event) {
        event.preventDefault();
        const start_time = strtime_to_minutes($('#start_time').val());
        const end_time = strtime_to_minutes($('#end_time').val());
        const smooth_transition = $('#smooth_transition').is(':checked');
        const duration = smooth_transition ? parseInt($('#duration').val()) : 0;
        if(end_time < start_time) {
            alert('Время окончания не может быть меньше времени начала!');
            return;
        }
        if (start_time === end_time) {
            alert('Время начала и окончания не может быть одинаковым!');
            return;
        }
        if (smooth_transition && duration * 2 > end_time - start_time) {
            alert('Длительность плавного перехода + включения не может быть больше времени между началом и окончанием!');
            return;
        }
        const data = {
            start_time: start_time,
            end_time: end_time,
            smooth_transition: smooth_transition,
            duration: duration
        };

        $.ajax({
            url: '/add_record',
            method: 'POST',
            contentType: 'application/json',
            data: JSON.stringify(data),
            success: function (record) {
                add_row_to_table(record.start_time, record.end_time, record.smooth_transition, record.duration);
            }
        });
    });
    //FIXME: После удаления записи, необходимо удалить её из таблицы
    $('#record_table').on('click', '.delete-btn', function() {
        const element = $(this).closest('tr');
        $.ajax({
            url: '/delete_record',
            method: 'POST',
            contentType: 'application/json',
            data: JSON.stringify({address: parseInt(element.attr('id'))}),
            success: function (record) {
                element.remove();
            }
        });
    });

    load_records();
});